#pragma once

#include <exception>
#include <functional>
#include <type_traits>
#include <tuple>
#include <memory>
#include <variant>
#include <cassert>

#include "traits.h"

namespace mfuture {

template <typename... T>
class Future;

template <typename... T, typename... U>
Future<T...> MakeReadyFuture(U&&...);

template <typename... T>
Future<T...> MakeExceptionalFuture(std::exception_ptr&&);

// FIXME(monte): variadic T is not the last?
template <typename... T, typename E>
Future<T...> MakeExceptionalFuture(E&&);

template <typename... T>
class Promise;

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
static_assert(IsFuture_v<Future<>&>);
static_assert(IsFuture_v<Future<>&&>);
static_assert(IsFuture_v<const Future<>&>);
static_assert(!IsFuture_v<>);
static_assert(!IsFuture_v<int>);

template <typename T>
using Futurized = std::conditional_t<
    IsFuture_v<T>, T,
    std::conditional_t<std::is_void_v<T>, Future<>, Future<T>>>;

static_assert(IsFuture_v<Futurized<void>>);
static_assert(IsFuture_v<Futurized<int>>);
static_assert(IsFuture_v<Futurized<Future<int>>>);

struct MakeReadyFutureTag {};
struct MakeExceptionalFutureTag {};

// Only for std::apply use.
template <typename... T>
Future<T...> MakeReadyFuture0(T&&... val) {
  return MakeReadyFuture<T...>(std::forward<T>(val)...);
}

template <typename F>
F MakeExceptionalFuture0(std::exception_ptr&& e) {
  static_assert(IsFuture_v<F>, "F should be a substantial future type.");
  return F(details::MakeExceptionalFutureTag{}, std::move(e));
}

template <typename... T>
class Continuation {
 public:
  // Ref: https://en.cppreference.com/w/cpp/memory/unique_ptr
  // If T is a derived class of some base B, then std::unique_ptr<T> is
  // implicitly convertible to std::unique_ptr<B>. The default deleter of the
  // resulting std::unique_ptr<B> will use operator delete for B, leading to
  // undefined behavior unless the destructor of B is virtual.
  virtual ~Continuation() {}

  virtual void Ready(std::tuple<T...>&& val) = 0;
  virtual void Fail(std::exception_ptr&& e) = 0;

 protected:
  template <typename Callback, typename V, typename PromiseType>
  static void Resolve(Callback&& cb, V&& v, PromiseType&& pr) {
    using R = typename internal::ClosureTraits<Callback>::ReturnType;
    static_assert(
        std::is_same_v<PromiseType, typename Futurized<R>::PromiseType>);

    // This promise might be destroyed in user callback. For example, the
    // original promise/future both are destroyed in the user callback, then
    // this continuation where this promise exists is also destroyed.
    PromiseType promise(std::move(pr));
    try {
      if constexpr (std::is_void_v<R>) {
        std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        promise.SetValue();  // FIMXE: noexcept?
      } else if constexpr (!IsFuture_v<R>) {
        auto r = std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        promise.SetValue(std::move(r));  // FIXME(monte): noexcept?
      } else {
        auto r = std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        r.Fold(promise);
      }
    } catch (...) {
      promise.SetException(std::current_exception());
    }
  }
};

template <typename Callback, typename... T>
class ContinuationWithValue : public Continuation<T...> {
  using R = typename internal::ClosureTraits<Callback>::ReturnType;
  using PromiseType = typename Futurized<R>::PromiseType;

  void Ready(std::tuple<T...>&& val) override {
    Continuation<T...>::Resolve(std::move(cb_), std::move(val), std::move(pr_));
  }

  void Fail(std::exception_ptr&& e) override {
    // skip cb_
    pr_.SetException(std::move(e));
  }

  Callback cb_;
  PromiseType pr_;

 public:
  ContinuationWithValue(Callback&& cb, PromiseType&& pr)
      : cb_(std::forward<Callback>(cb)), pr_(std::move(pr)) {}

  ~ContinuationWithValue() override {}
};

template <typename Callback, typename... T>
class ContinuationWithFuture : public Continuation<T...> {
  using R = typename internal::ClosureTraits<Callback>::ReturnType;
  using PromiseType = typename Futurized<R>::PromiseType;

  void Ready(std::tuple<T...>&& val) override {
    auto ft = std::apply(MakeReadyFuture0<T...>,
                         std::forward<std::tuple<T...>>(std::move(val)));
    // We must use make_tuple instead of forward_as_tuple here, because
    // forward_as_tuple never extend arguments' lifetime, so in case of running
    // callback in an asynchronous executor, the tuple value will become
    // dangling.
    Continuation<T...>::Resolve(std::move(cb_), std::make_tuple(std::move(ft)),
                                std::move(pr_));
  }

  void Fail(std::exception_ptr&& e) override {
    auto ft = MakeExceptionalFuture<T...>(std::move(e));
    Continuation<T...>::Resolve(std::move(cb_), std::make_tuple(std::move(ft)),
                                std::move(pr_));
  }

  Callback cb_;
  PromiseType pr_;

 public:
  ContinuationWithFuture(Callback&& cb, PromiseType&& pr)
      : cb_(std::forward<Callback>(cb)), pr_(std::move(pr)) {}

  ~ContinuationWithFuture() override {}
};

template <typename... T>
class FutureState {
  static_assert(sizeof...(T) == 0 || internal::IsDefaultConstructible_v<T...>,
                "Types should be default-constructible.");

 public:
  template <typename... U>
  void SetValue(U&&... val) {
    assert(inner_->state == 0);
    inner_->state = 1;
    // Ref: https://en.cppreference.com/w/cpp/language/dependent_name
    inner_->result.template emplace<0>(std::forward<U>(val)...);
    TrySchedule();
  }

  void SetValue(std::tuple<T...>&& val) {
    assert(inner_->state == 0);
    inner_->state = 1;
    inner_->result.template emplace<0>(std::move(val));
    TrySchedule();
  }

  void SetValue(const std::tuple<T...>& val) {
    assert(inner_->state == 0);
    inner_->state = 1;
    inner_->result.template emplace<0>(val);
    TrySchedule();
  }

  void SetException(std::exception_ptr&& e) {
    assert(inner_->state == 0);
    inner_->state = 2;
    inner_->result.template emplace<1>(std::move(e));
    TrySchedule();
  }

  // TODO(monte): We should simplify this while it's resolved to reduce
  // the cost of continuation. Furthermore, we should support empty resolved
  // future for such as MakeReadyFuture...
  template <typename Callback>
  auto SetCallback(Callback&& cb) {
    assert(!inner_->consumer);

    using R = typename internal::ClosureTraits<Callback>::ReturnType;
    using ArgTuple = typename internal::ClosureTraits<Callback>::ArgTypes;
    // TODO(monte): To be more friendly, continuation type limit: lambda,
    // args...

    static_assert(!internal::IsNonConstLvalueReference_v<ArgTuple>,
                  "Continuation's parameters are NOT allowed to be non-const "
                  "lvalue reference.");

    typename Futurized<R>::PromiseType pr;
    auto ft = pr.GetFuture();

    using RawArgs = internal::RemoveCVRef_t<ArgTuple>;
    using Expected1 = std::tuple<T...>;
    using Expected2 = std::tuple<Future<T...>>;

    if constexpr (std::is_same_v<RawArgs, Expected1>) {
      inner_->consumer =
          std::make_unique<ContinuationWithValue<Callback, T...>>(
              std::forward<Callback>(cb), std::move(pr));
    } else {
      static_assert(std::is_same_v<RawArgs, Expected2>,
                    "Continuation's parameters' types are invalid.");
      inner_->consumer =
          std::make_unique<ContinuationWithFuture<Callback, T...>>(
              std::forward<Callback>(cb), std::move(pr));
    }

    TrySchedule();
    return ft;
  }

  int GetState() const noexcept {
    assert(inner_->state >= 0);
    assert(inner_->state <= 2);
    return inner_->state;
  }

  auto&& GetResult() noexcept {
    assert(!inner_->result_moved);
    inner_->result_moved = true;
    return std::move(inner_->result);
  }

 private:
  void TrySchedule() {
    if (!inner_->consumer || !inner_->state) return;

    assert(!inner_->result_moved);
    inner_->result_moved = true;

    if (inner_->state == 1) {
      inner_->consumer->Ready(std::get<0>(std::move(inner_->result)));
    } else {  // 2
      inner_->consumer->Fail(std::get<1>(std::move(inner_->result)));
    }
    // After scheduling, this future state might has been released, so we can't
    // access it anymore.
  }

  friend class Future<T...>;

 private:
  struct Inner {
    // Producer
    int state{0};  // 0: unresolved, 1: ready, 2: failed
    std::variant<std::tuple<T...>, std::exception_ptr> result;
    bool result_moved{false};

    // Continuation's type info is unknown until callback set(i.e. runtime), but
    // it's necessary while compiling resolving code(i.e. SetValue,
    // SetException), therefore we can only rely on runtime polymorphism.
    std::unique_ptr<Continuation<T...>> consumer;
  } this_inner_;

  // Effective entity
  struct Inner* inner_ = &this_inner_;
  std::shared_ptr<FutureState<T...>> holder_;
};

}  // namespace details

// User should explicitly assign template argument, for the sake of avoiding
// unexpected template argument deduction(e.g. reference type), at the other
// hand, we want to keep arguments' type, so we can rely on the arguments
// deduction with U&&.
//
// FIXME(monte): Is it valid to declare multiple template param pack? Reference
// to seastar::make_ready_future. Ref:
// https://stackoverflow.com/questions/9831501/how-can-i-have-multiple-parameter-packs-in-a-variadic-template
template <typename... T, typename... U>
Future<T...> MakeReadyFuture(U&&... val) {
  return Future<T...>(details::MakeReadyFutureTag{}, std::forward<U>(val)...);
}

template <typename... T>
Future<T...> MakeExceptionalFuture(std::exception_ptr&& e) {
  return Future<T...>(details::MakeExceptionalFutureTag{}, std::move(e));
}

// FIXME(monte): Is it valid to make template param pack before others?
template <typename... T, typename E>
Future<T...> MakeExceptionalFuture(E&& e) {
  return Future<T...>(details::MakeExceptionalFutureTag{},
                      std::make_exception_ptr(std::forward<E>(e)));
}

// TODO(monte): Add executor support, caring about the thread-safety
template <typename Function, typename... Args>
auto FuturizeInvoke(Function&& f, Args&&... args) {
  using R = typename internal::ClosureTraits<Function>::ReturnType;
  try {
    if constexpr (details::IsFuture_v<R>) {
      return std::invoke(std::forward<Function>(f),
                         std::forward<Args>(args)...);
    } else if constexpr (std::is_void_v<R>) {
      std::invoke(std::forward<Function>(f), std::forward<Args>(args)...);
      return MakeReadyFuture<>();
    } else {
      auto r =
          std::invoke(std::forward<Function>(f), std::forward<Args>(args)...);
      return MakeReadyFuture<R>(std::move(r));
    }
  } catch (...) {
    if constexpr (details::IsFuture_v<R>)
      return details::MakeExceptionalFuture0<R>(std::current_exception());
    else if constexpr (std::is_void_v<R>)
      return MakeExceptionalFuture<>(std::current_exception());
    else
      return MakeExceptionalFuture<R>(std::current_exception());
  }
}

template <typename Function, typename Tuple>
auto FuturizeApply(Function&& f, Tuple&& t) {
  using R = typename internal::ClosureTraits<Function>::ReturnType;
  try {
    if constexpr (details::IsFuture_v<R>) {
      return std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
    } else if constexpr (std::is_void_v<R>) {
      std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
      return MakeReadyFuture<>();
    } else {
      auto r = std::apply(std::forward<Function>(f), std::forward<Tuple>(t));
      return MakeReadyFuture<R>(std::move(r));
    }
  } catch (...) {
    if constexpr (details::IsFuture_v<R>)
      return details::MakeExceptionalFuture0<R>(std::current_exception());
    else if constexpr (std::is_void_v<R>)
      return MakeExceptionalFuture<>(std::current_exception());
    else
      return MakeExceptionalFuture<R>(std::current_exception());
  }
}

template <typename... T>
class Promise;

template <typename... T>
class Future {
  static_assert(internal::IsNotVoid_v<T...>,
                "Future's template arguments are NOT allowed to be void, use "
                "Future<> instead of Future<void>.");

  static_assert(internal::IsNotReference_v<T...>,
                "Future's template arguments are NOT allowed to be reference.");

 public:
  using PromiseType = Promise<T...>;
  using TupleValueType = std::tuple<T...>;

  Future(Future&& other) = default;
  Future& operator=(Future&& other) = default;

  template <typename Callback>
  auto Then(Callback&& cb) noexcept {
    if (IsResolved())
      return Schedule(std::forward<Callback>(cb));
    else
      return state_->SetCallback(std::forward<Callback>(cb));
  }

  bool IsReady() const noexcept { return state_->GetState() == 1; }

  bool IsFailed() const noexcept { return state_->GetState() == 2; }

  bool IsResolved() const noexcept { return state_->GetState() != 0; }

  auto GetValue() {  // FIXME(monte): return type?
    assert(IsReady());
    return std::get<0>(state_->GetResult());
  }

  template <std::size_t I, typename = std::enable_if_t<(sizeof...(T) > I)>>
  auto GetValue() {  // FIXME(monte): return type?
    assert(IsReady());
    auto&& t = std::get<0>(state_->GetResult());
    return std::get<I>(std::move(t));
  }

  auto GetException() {  // FIXME(monte): return type? how to unwrap
                         // std::exception_ptr
    assert(IsFailed());
    return std::get<1>(state_->GetResult());
  }

  void Fold(Promise<T...>& promise) {
    if (IsReady()) {
      promise.SetValue(GetValue());
    } else if (IsFailed()) {
      promise.SetException(GetException());
    } else {
      // In order to support multilevel folding, we chain this entity by
      // promise's effective entity.
      state_->inner_ = promise.state_->inner_;
      state_->holder_ =
          promise.state_->holder_ ? promise.state_->holder_ : promise.state_;
      // Now this future entity shoulders all the keeper's duty. That's
      // reasonable, because once this future entity is destroyed, it's
      // meaningless to keep the downstream entity.
    }
  }

  // Only used in `Promise<Future<X...>, Y...>`/`Future<Future<X...>, Y...>`,
  // because `FutureState<Future<X...>, Y...>` needs to be
  // default-constructible.
  Future() {}

 private:
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;

  Future(std::shared_ptr<details::FutureState<T...>> state)
      : state_(std::move(state)) {}

  template <typename... U>
  Future(details::MakeReadyFutureTag, U&&... val) {
    state_ = std::make_shared<details::FutureState<T...>>();
    state_->SetValue(std::forward<U>(val)...);
  }

  Future(details::MakeExceptionalFutureTag, std::exception_ptr&& e) {
    state_ = std::make_shared<details::FutureState<T...>>();
    state_->template SetException(std::move(e));
  }

  template <typename Callback>
  auto Schedule(Callback&& cb) {
    using R = typename internal::ClosureTraits<Callback>::ReturnType;
    using ArgTuple = typename internal::ClosureTraits<Callback>::ArgTypes;

    // TODO(monte): To be more friendly, continuation type limit: lambda,
    // args...

    static_assert(!internal::IsNonConstLvalueReference_v<ArgTuple>,
                  "Continuation's parameters are NOT allowed to be non-const "
                  "lvalue reference.");

    using RawArgs = internal::RemoveCVRef_t<ArgTuple>;
    using Expected1 = std::tuple<T...>;
    using Expected2 = std::tuple<Future<T...>>;

    if constexpr (std::is_same_v<RawArgs, Expected1>) {
      if (IsReady()) {
        return FuturizeApply(std::forward<Callback>(cb), GetValue());
      } else {  // IsFailed
        if constexpr (details::IsFuture_v<R>)
          return details::MakeExceptionalFuture0<R>(GetException());
        else if constexpr (std::is_void_v<R>)
          return MakeExceptionalFuture<>(GetException());
        else
          return MakeExceptionalFuture<R>(GetException());
      }
    } else {
      static_assert(std::is_same_v<RawArgs, Expected2>,
                    "Continuation's parameters' types are invalid.");
      return FuturizeInvoke(std::forward<Callback>(cb), Future<T...>(state_));
    }
  }

  template <typename... U, typename... V>
  friend Future<U...> MakeReadyFuture(V&&...);

  template <typename... U>
  friend Future<U...> MakeExceptionalFuture(std::exception_ptr&&);

  template <typename... U, typename E>
  friend Future<U...> MakeExceptionalFuture(E&&);

  friend Future<T...> details::MakeExceptionalFuture0<>(std::exception_ptr&&);

  friend class Promise<T...>;

 private:
  std::shared_ptr<details::FutureState<T...>> state_;
};

template <typename... T>
class Promise {
  static_assert(internal::IsNotVoid_v<T...>,
                "Promise's template arguments are NOT allowed to be void, use "
                "Promise<> instead of Promise<void>.");
  static_assert(
      internal::IsNotReference_v<T...>,
      "Promise's template arguments are NOT allowed to be reference.");

 public:
  Promise() : state_(std::make_shared<details::FutureState<T...>>()) {}

  Promise(Promise&& other) = default;
  Promise& operator=(Promise&& other) = default;

  Future<T...> GetFuture() {
    assert(!future_got_);
    future_got_ = true;
    return Future<T...>(state_);
  }

  template <typename... U>
  void SetValue(U&&... val) {
    state_->SetValue(std::forward<U>(val)...);
  }

  void SetValue(std::tuple<T...>&& val) {
#if 1
    state_->SetValue(std::move(val));
#else
    // We can also call the overloaded version.
    // Ref:
    // https://stackoverflow.com/questions/44776927/call-member-function-with-expanded-tuple-parms-stdinvoke-vs-stdapply
    std::apply([this](T&&... val) { SetValue(std::forward<T>(val)...); },
               std::move(val));
#endif
  }

  // General version, `const &` may be bound to any value.
  void SetValue(const std::tuple<T...>& val) { state_->SetValue(val); }

  void SetException(std::exception_ptr&& e) {
    state_->SetException(std::move(e));
  }

  template <typename E>
  void SetException(E&& e) {
    state_->SetException(std::make_exception_ptr(std::forward<E>(e)));
  }

 private:
  Promise(const Promise&) = delete;
  Promise& operator=(const Promise&) = delete;

  friend class Future<T...>;

 private:
  std::shared_ptr<details::FutureState<T...>> state_;
  bool future_got_{false};
};

// static_assert(details::IsDefaultConstructible_v<Future<>>);  // FIXME(monte):
// false?
static_assert(internal::IsDefaultConstructible_v<Promise<>>);
static_assert(internal::IsDefaultConstructible_v<Future<bool>>);
static_assert(internal::IsDefaultConstructible_v<Promise<int>>);

namespace details {

template <typename Stop, typename Function>
struct DoUntilState {
  DoUntilState(Stop&& stop, Function&& function)
      : stop_(std::forward<Stop>(stop)),
        function_(std::forward<Function>(function)) {}

  void SetFailed(std::exception_ptr&& e) {
    promise_.SetException(std::move(e));
    delete this;
  }

  Future<> GetFuture() { return promise_.GetFuture(); }

  void Run() {
    do {
      if (stop_()) {
        promise_.SetValue();
        delete this;
        break;
      } else {
        auto future = FuturizeInvoke(function_);
        if (future.IsReady()) {
          // Never use Then() to drive here to avoid stack overflow.
        } else if (future.IsFailed()) {
          promise_.SetException(future.GetException());
          delete this;
          break;
        } else {
          // Return value ignored.
          future.Then([this](Future<>&& ft) {
            if (ft.IsFailed()) {
              SetFailed(ft.GetException());
              return;
            }
            assert(ft.IsReady());
            Run();
          });
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
Future<> DoUntil(Stop&& stop, Function&& function) {
  static_assert(std::is_convertible_v<std::invoke_result_t<Stop>, bool>);

  // If Function doesn't return a Future, user should use do-while instead.
  using R = std::invoke_result_t<Function>;
  static_assert(std::is_same_v<R, Future<>>);

  do {  // Fast path.
    if (stop()) return MakeReadyFuture<>();
    auto future = FuturizeInvoke(function);
    if (future.IsReady())
      continue;
    else if (future.IsFailed())
      return future;
    else {
      auto state = new details::DoUntilState<Stop, Function>(
          std::forward<Stop>(stop), std::forward<Function>(function));
      auto ret = state->GetFuture();
      // Return value ignored.
      future.Then([state](Future<>&& ft) {
        if (ft.IsFailed()) {
          state->SetFailed(ft.GetException());
          return;
        }
        assert(ft.IsReady());
        state->Run();
      });
      return ret;
    }
  } while (true);
}

}  // namespace mfuture