#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>

#include "traits.h"

namespace nfuture {

template <class... T>
class Promise;

template <class... T>
class Future;

namespace details {

struct Tag;  // Is incomplete type OK?

template <typename... T>
struct IsFuture : std::false_type {};

template <typename... T>
struct IsFuture<Future<T...>, Tag> : std::true_type {};

template <typename T>
struct IsFuture<T> : IsFuture<internal::RemoveCVRef_t<T>, Tag> {};

template <typename... T>
constexpr bool IsFuture_v = IsFuture<T...>::value;

static_assert(IsFuture_v<Future<>>);
static_assert(IsFuture_v<Future<int>>);
static_assert(IsFuture_v<Future<> &>);
static_assert(IsFuture_v<Future<> &&>);
static_assert(IsFuture_v<const Future<> &>);
static_assert(!IsFuture_v<>);
static_assert(!IsFuture_v<int>);

template <typename T>
using Futurized = std::conditional_t<IsFuture_v<T>, T, Future<T>>;

static_assert(IsFuture_v<Futurized<void>>);
static_assert(IsFuture_v<Futurized<int>>);
static_assert(IsFuture_v<Futurized<Future<int>>>);

struct MakeReadyFutureTag {};
struct MakeExceptionalFutureTag {};

template <class... T>
class FutureState {
 public:
  FutureState() = default;

  template <class... U>
  FutureState(MakeReadyFutureTag, U &&...value)
      : state_(State::kValue), value_(std::forward<U>(value)...) {}

  FutureState(MakeExceptionalFutureTag, std::exception_ptr exception)
      : state_(State::kException), exception_(exception) {}

  FutureState(FutureState &&other) { MoveFrom(std::move(other)); }

  FutureState &operator=(FutureState &&other) {
    if (this != &other) MoveFrom(std::move(other));
    return *this;
  }

  void Reset() { state_ = State::kEmpty; }

  void SetValue(std::tuple<T...> &&value) {
    value_ = std::move(value);
    state_ = State::kValue;
  }

  template <class... U>
  void SetValue(U &&...value) {
    value_ = std::make_tuple(std::move(value)...);
    state_ = State::kValue;
  }

  void SetException(std::exception_ptr exception) {
    exception_ = exception;
    state_ = State::kException;
  }

  void SetInvalid() { state_ = State::kInvalid; }

  bool Valid() const { return state_ != State::kInvalid; }
  bool Empty() const { return state_ == State::kEmpty; }
  bool Ready() const { return state_ == State::kValue; }
  bool Failed() const { return state_ == State::kException; }
  bool Available() const { return Ready() || Failed(); }

  std::tuple<T...> &&Value() && {
    assert(Ready());
    return std::move(value_);
  }

  std::tuple<T...> &Value() & {
    assert(Ready());
    return value_;
  }

  std::exception_ptr Exception() {
    assert(Failed());
    return exception_;
  }

 private:
  void MoveFrom(FutureState &&other) {
    state_ = std::exchange(other.state_, State::kInvalid);
    switch (state_) {
      case State::kValue:
        value_ = std::move(other.value_);
        break;
      case State::kException:
        exception_ = std::exchange(other.exception_, nullptr);
        break;
      default:
        break;
    }
  }

  FutureState(const FutureState &) = delete;
  FutureState &operator=(const FutureState &) = delete;

  friend class Promise<T...>;
  friend class Future<T...>;

 private:
  enum class State {
    kEmpty,
    kValue,
    kException,
    kInvalid
  } state_{State::kEmpty};
  std::tuple<T...> value_;
  std::exception_ptr exception_;
};

struct ContinuationBase {
  virtual ~ContinuationBase() = default;
  virtual void Run() = 0;
};

template <class Callback, class... T>
struct Continuation : public ContinuationBase {
  Continuation(Callback &&callback)
      : callback_(std::forward<Callback>(callback)) {}

  void Run() override {
    static_assert(
        std::is_void_v<std::invoke_result_t<Callback, FutureState<T...> &&>>);
    std::invoke(callback_, std::move(state_));
    delete this;
  }

  Callback callback_;
  FutureState<T...> state_;
};

// Perhaps users also need this function.
template <typename FutureType>
FutureType MakeExceptionalFuture(std::exception_ptr exception) {
  return FutureType(details::MakeExceptionalFutureTag{}, std::move(exception));
}

}  // namespace details

template <typename Function, typename... Args>
auto FuturizeInvoke(Function &&f, Args &&...args);

template <typename Function, typename Tuple>
auto FuturizeApply(Function &&f, Tuple &&t);

template <typename... T, typename... U>
Future<T...> MakeReadyFuture(U &&...val) {
  return Future<T...>(details::MakeReadyFutureTag{}, std::forward<U>(val)...);
}

template <typename... T>
Future<T...> MakeExceptionalFuture(std::exception_ptr exception) {
  return Future<T...>(details::MakeExceptionalFutureTag{},
                      std::move(exception));
}

template <class... T>
class Future {
  static_assert(internal::IsNotVoid_v<T...>,
                "Future's template arguments are NOT allowed to be void, use "
                "Future<> instead of Future<void>.");

  static_assert(internal::IsNotReference_v<T...>,
                "Future's template arguments are NOT allowed to be reference.");

 public:
  using PromiseType = Promise<T...>;

  Future() : promise_(nullptr) {}

  Future(Future &&other) { MoveFrom(std::move(other)); }

  Future &operator=(Future &&other) {
    if (this != &other) {
      Reset();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  ~Future() { Reset(); }

  template <class Callback, class R = std::invoke_result_t<Callback, T &&...>>
  auto Then(Callback &&callback) {
    // TODO(monte): Detect another Then.
    using FR =
        std::conditional_t<std::is_void_v<R>, Future<>, details::Futurized<R>>;
    if (state_.Ready()) {
      return FuturizeApply(std::forward<Callback>(callback),
                           std::move(state_).Value());
    } else if (state_.Failed()) {
      return details::MakeExceptionalFuture<FR>(state_.Exception());
    } else {
      assert(promise_);
      assert(!promise_->continuation_);

      FR future;
      auto cb = [promise = future.GetPromise(),
                 callback = std::forward<Callback>(callback)](
                    details::FutureState<T...> &&state) mutable {
        if (state.Ready()) {
          if constexpr (details::IsFuture_v<R>) {
            std::apply(callback, std::move(state).Value())
                .Fold(std::move(promise));
          } else if constexpr (std::is_void_v<R>) {
            std::apply(callback, std::move(state).Value());
            promise.SetValue();
          } else {
            promise.SetValue(std::apply(callback, std::move(state).Value()));
          }
        } else {
          assert(state.Failed());
          promise.SetException(std::move(state).Exception());
        }
      };
      auto continuation =
          new details::Continuation<decltype(cb), T...>(std::move(cb));

      promise_->continuation_ = continuation;
      state_.SetInvalid();
      promise_->p_state_ = &continuation->state_;

      return future;  // NRVO?
    }
  }

  template <class Callback,
            class R = std::invoke_result_t<Callback, Future<T...> &&>>
  auto ThenWrap(Callback &&callback) {
    using FR =
        std::conditional_t<std::is_void_v<R>, Future<>, details::Futurized<R>>;
    if (state_.Available()) {
      return FuturizeInvoke(std::forward<Callback>(callback), std::move(*this));
    } else {
      assert(promise_);
      assert(!promise_->continuation_);

      FR future;
      auto cb = [promise = future.GetPromise(),
                 callback = std::forward<Callback>(callback)](
                    details::FutureState<T...> &&state) mutable {
        if constexpr (details::IsFuture_v<R>) {
          std::invoke(callback, Future(std::move(state)))
              .Fold(std::move(promise));
        } else if constexpr (std::is_void_v<R>) {
          std::invoke(callback, Future(std::move(state)));
          promise.SetValue();
        } else {
          promise.SetValue(std::invoke(callback, Future(std::move(state))));
        }
      };
      auto continuation =
          new details::Continuation<decltype(cb), T...>(std::move(cb));

      promise_->continuation_ = continuation;
      state_.SetInvalid();
      promise_->p_state_ = &continuation->state_;

      return future;  // NRVO?
    }
  }

  bool Available() const { return state_.Available(); }
  bool Ready() const { return state_.Ready(); }
  bool Failed() const { return state_.Failed(); }

  std::tuple<T...> &&Value() { return std::move(state_).Value(); }

  template <size_t Index>
  auto Value() {
    return std::get<Index>(Value());
  }

  std::exception_ptr Exception() { return state_.Exception(); }

 private:
  friend class Promise<T...>;

  template <typename... U>
  friend class Future;

  template <typename... U, typename... V>
  friend Future<U...> MakeReadyFuture(V &&...);

  template <typename... U>
  friend Future<U...> MakeExceptionalFuture(std::exception_ptr);

  template <typename FutureType>
  friend FutureType details::MakeExceptionalFuture(std::exception_ptr);

  template <typename... U>
  Future(details::MakeReadyFutureTag tag, U &&...val)
      : state_(tag, std::move(val)...), promise_(nullptr) {}

  Future(details::MakeExceptionalFutureTag tag, std::exception_ptr exception)
      : state_(tag, exception), promise_(nullptr) {}

  Future(Promise<T...> *promise)
      : state_(std::move(*(promise->p_state_))), promise_(promise) {
    promise_->p_state_ = &state_;
    promise_->future_ = this;
  }

  Future(details::FutureState<T...> &&state)
      : state_(std::move(state)), promise_(nullptr) {}

  void Reset() {
    if (promise_) {
      promise_->future_ = nullptr;
      if (promise_->p_state_ == &state_) {
        assert(!promise_->continuation_);
        promise_->p_state_ = nullptr;
      }
    }
  }

  void MoveFrom(Future<T...> &&other) {
    // Make `other` invalid. It's still reusable if `Reset` is called later on.
    state_ = std::move(other.state_);
    promise_ = std::exchange(other.promise_, nullptr);
    if (promise_) {
      promise_->future_ = this;
      if (promise_->p_state_ == &other.state_) promise_->p_state_ = &state_;
    }
  }

  // Make it public?
  Promise<T...> GetPromise() {
    assert(!promise_);
    return Promise<T...>(this);
  }

  // TODO(monte): Make it public.
  void Fold(Promise<T...> &&promise) {
    if (state_.Ready()) {
      promise.SetValue(std::move(state_).Value());
    } else if (state_.Failed()) {
      promise.SetException(state_.Exception());
    } else {
      assert(promise_);
      *promise_ = std::move(promise);
    }
  }

 private:
  details::FutureState<T...> state_;
  Promise<T...> *promise_;
};

template <>
class Future<void> : public Future<> {};

template <class... T>
class Promise {
  static_assert(internal::IsNotVoid_v<T...>,
                "Promise's template arguments are NOT allowed to be void, use "
                "Promise<> instead of Future<void>.");

  static_assert(
      internal::IsNotReference_v<T...>,
      "Promise's template arguments are NOT allowed to be reference.");

 public:
  Promise() : p_state_(&state_), future_(nullptr), continuation_(nullptr) {}

  Promise(Promise &&other) { MoveFrom(std::move(other)); }

  ~Promise() { Reset(); }

  Future<T...> GetFuture() {
    assert(!future_);
    assert(p_state_);
    assert(!continuation_);
    return Future<T...>(this);
  }

  template <class... U>
  void SetValue(U &&...value) {
    // In case that the counterpart Future has been destructed, such as the ones
    // returned by Then() abandoned by user.
    if (!p_state_) return;

    assert(p_state_->Empty());
    p_state_->SetValue(std::forward<U>(value)...);

    // Clear the continuation member before scheduling, because this promise
    // might be destructed before the continuation is done.
    if (auto continuation = std::exchange(continuation_, nullptr)) {
      // TODO(monte): Schedule?
      continuation->Run();
    }
  }

  void SetException(std::exception_ptr exception) {
    // In case that the counterpart Future has been destructed, such as the ones
    // returned by Then() abandoned by user.
    if (!p_state_) return;

    assert(p_state_->Empty());
    p_state_->SetException(exception);

    // Clear the continuation member before scheduling, because this promise
    // might be destructed before the continuation is done.
    if (auto continuation = std::exchange(continuation_, nullptr)) {
      // TODO(monte): Schedule?
      continuation->Run();
    }
  }

 private:
  friend class Future<T...>;

  Promise(Future<T...> *future)
      : p_state_(&future->state_), future_(future), continuation_(nullptr) {
    state_.SetInvalid();
    future_->promise_ = this;
  }

  Promise &operator=(Promise &&other) {
    if (this != &other) {
      Reset();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  void Reset() {
    assert(!continuation_);
    if (future_) {
      future_->promise_ = nullptr;
      future_ = nullptr;
    }
    state_.Reset();
    p_state_ = &state_;
  }

  void MoveFrom(Promise &&other) {
    // Make `other` invalid. It's still reusable if `Reset` is called later on.
    if (other.p_state_ == &other.state_) {
      state_ = std::move(other.state_);
    }

    p_state_ = std::exchange(other.p_state_, nullptr);
    future_ = std::exchange(other.future_, nullptr);
    continuation_ = std::exchange(other.continuation_, nullptr);

    if (future_) {
      future_->promise_ = this;
    }
  }

 private:
  details::FutureState<T...> state_;
  details::FutureState<T...> *p_state_;
  Future<T...> *future_;
  details::ContinuationBase *continuation_;
};

template <>
class Promise<void> : public Promise<> {};

template <typename Function, typename... Args>
auto FuturizeInvoke(Function &&f, Args &&...args) {
  using R = std::invoke_result_t<Function, decltype(args)...>;
  if constexpr (details::IsFuture_v<R>) {
    return std::invoke(std::forward<Function>(f), std::forward<Args>(args)...);
  } else if constexpr (std::is_void_v<R>) {
    std::invoke(std::forward<Function>(f), std::forward<Args>(args)...);
    return MakeReadyFuture<>();
  } else {
    auto r =
        std::invoke(std::forward<Function>(f), std::forward<Args>(args)...);
    // FIXME(monte): R is a reference type?
    return MakeReadyFuture<R>(std::move(r));
  }
}

template <typename Function, typename Tuple>
auto FuturizeApply(Function &&f, Tuple &&t) {
  using R = internal::ApplyResultType<Function, Tuple>;
  if constexpr (details::IsFuture_v<R>) {
    return std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
  } else if constexpr (std::is_void_v<R>) {
    std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
    return MakeReadyFuture<>();
  } else {
    auto r = std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
    return MakeReadyFuture<R>(std::move(r));
  }
}

namespace details {

template <typename Stop, typename Function>
struct DoUntilState {
  DoUntilState(Stop &&stop, Function &&function)
      : stop_(std::forward<Stop>(stop)),
        function_(std::forward<Function>(function)) {}

  Future<> GetFuture() { return promise_.GetFuture(); }

  void Run() {
    do {
      if (stop_()) {
        promise_.SetValue();
        delete this;
        break;
      } else {
        auto future = FuturizeInvoke(function_);
        if (future.Ready()) {
          // Never use Then() to drive here to avoid stack overflow.
        } else if (future.Failed()) {
          promise_.SetValue();
          delete this;
          break;
        } else {
          // Return value ignored.
          future.Then([this]() { Run(); });
          break;
        }
      }
    } while (true);
  }

 private:
  Promise<> promise_;
  Stop stop_;
  Function function_;
};

}  // namespace details

template <typename Stop, typename Function>
Future<> DoUntil(Stop &&stop, Function &&function) {
  static_assert(std::is_convertible_v<std::invoke_result_t<Stop>, bool>);

  // If Function doesn't return a Future, user should use do-while instead.
  using R = std::invoke_result_t<Function>;
  static_assert(std::is_same_v<R, Future<>>);

  do {  // Fast path.
    if (stop()) return MakeReadyFuture<>();
    auto future = FuturizeInvoke(function);
    if (future.Ready())
      continue;
    else if (future.Failed())
      return future;
    else {
      auto state = new details::DoUntilState<Stop, Function>(
          std::forward<Stop>(stop), std::forward<Function>(function));
      auto ret = state->GetFuture();
      // Return value ignored.
      future.Then([state]() { state->Run(); });
      return ret;
    }
  } while (true);
}

}  // namespace nfuture