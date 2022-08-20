#pragma once

#include <exception>
#include <type_traits>
#include <tuple>
#include <memory>
#include <functional>
#include <any>
#include <variant>
#include <cassert>

namespace mfuture {

template <typename... T>
class Future;

template <typename... T, typename... U>
Future<T...> MakeReadyFuture(U&&...);

template <typename... T>
Future<T...> MakeExceptionFuture(std::exception_ptr&&);

// FIXME: variadic T is not the last?
template <typename... T, typename E>
Future<T...> MakeExceptionFuture(E&&);

class Executor {
 public:
  virtual void Run() = 0;
};

template <typename... T>
class Promise;

namespace details {

template <typename T>
using Debug = typename T::print_type;

template <typename T>
struct RemoveCVRef {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename... T>
using RemoveCVRef_t = typename RemoveCVRef<T...>::type;

template <typename... T>
struct RemoveCVRef<std::tuple<T...>> {
  using type = std::tuple<RemoveCVRef_t<T>...>;
};

struct Tag;  // Is incomplete type OK?

template <typename... T>
struct IsFuture : std::false_type {};

template <typename... T>
struct IsFuture<Future<T...>, Tag> : std::true_type {};

template <typename T>
struct IsFuture<T> : IsFuture<RemoveCVRef_t<T>, Tag> {};

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
using Futurized = std::conditional_t<IsFuture_v<T>, T, std::conditional_t<std::is_void_v<T>, Future<>, Future<T>>>;

static_assert(IsFuture_v<Futurized<void>>);
static_assert(IsFuture_v<Futurized<int>>);
static_assert(IsFuture_v<Futurized<Future<int>>>);

// Ref:
// https://stackoverflow.com/questions/36329826/how-to-specialize-template-class-without-parameters
// https://stackoverflow.com/questions/24108852/how-to-check-if-all-of-variadic-template-arguments-have-special-function
template <typename... T>
constexpr bool IsReference_v = false;

template <typename T>
constexpr bool IsReference_v<T> = std::is_reference_v<T>;

template <typename T, typename... U>
constexpr bool IsReference_v<T, U...> = std::is_reference_v<T>&& IsReference_v<U...>;

static_assert(!IsReference_v<>);
static_assert(!IsReference_v<void>);
static_assert(!IsReference_v<int, int&>);
static_assert(IsReference_v<int&>);
static_assert(IsReference_v<int&, bool&&, const long&, const float&&>);

template <typename... T>
constexpr bool IsNotReference_v = true;

template <typename T>
constexpr bool IsNotReference_v<T> = !std::is_reference_v<T>;

template <typename T, typename... U>
constexpr bool IsNotReference_v<T, U...> = !std::is_reference_v<T> && IsNotReference_v<U...>;

static_assert(IsNotReference_v<>);
static_assert(IsNotReference_v<void>);
static_assert(IsNotReference_v<int, const long, void*>);
static_assert(!IsNotReference_v<int&>);
static_assert(!IsNotReference_v<const int&>);
static_assert(!IsNotReference_v<int&&>);
static_assert(!IsNotReference_v<const int&&>);
static_assert(!IsNotReference_v<int, int&>);

template <typename... T>
constexpr bool IsNotVoid_v = true;

template <typename T>
constexpr bool IsNotVoid_v<T> = !std::is_void_v<T>;

template <typename T, typename... U>
constexpr bool IsNotVoid_v<T, U...> = IsNotVoid_v<T>&& IsNotVoid_v<U...>;

static_assert(IsNotVoid_v<>);
static_assert(IsNotVoid_v<bool, void*, std::nullptr_t>);
static_assert(!IsNotVoid_v<void>);
static_assert(!IsNotVoid_v<bool, void>);

template <typename... T>
constexpr bool IsNonConstLvalueReference_v = false;

template <typename T>
constexpr bool IsNonConstLvalueReference_v<const T&> = false;

template <typename T>
constexpr bool IsNonConstLvalueReference_v<T&> = true;

template <typename T, typename... U>
constexpr bool IsNonConstLvalueReference_v<std::tuple<T, U...>> =
    IsNonConstLvalueReference_v<T>&& IsNonConstLvalueReference_v<U...>;

template <typename... T>
constexpr bool IsDefaultConstructible_v = false;

template <typename T, typename... U>
constexpr bool IsDefaultConstructible_v<T, U...> = std::is_default_constructible_v<T>&& IsDefaultConstructible_v<U...>;

template <typename T>
constexpr bool IsDefaultConstructible_v<T> = std::is_default_constructible_v<T>;

static_assert(!IsDefaultConstructible_v<>);
static_assert(!IsDefaultConstructible_v<void>);
static_assert(IsDefaultConstructible_v<int>);
static_assert(IsDefaultConstructible_v<int, bool>);

template <typename T>
struct ClosureTraits : public ClosureTraits<decltype(&T::operator())> {};

template <typename R>
struct ClosureTraits<R()> {
  using ReturnType = R;
  using ArgType = void;
  using ArgTypes = std::tuple<>;
};

template <typename C, typename R>
struct ClosureTraits<R (C::*)()> {
  using ReturnType = R;
  using ArgType = void;
  using ArgTypes = std::tuple<>;
};

template <typename C, typename R>
struct ClosureTraits<R (C::*)() const> {
  using ReturnType = R;
  using ArgType = void;
  using ArgTypes = std::tuple<>;
};

template <typename R, typename Arg>
struct ClosureTraits<R(Arg)> {
  using ReturnType = R;
  using ArgType = Arg;
  using ArgTypes = std::tuple<Arg>;
};

template <typename C, typename R, typename Arg>
struct ClosureTraits<R (C::*)(Arg)> {
  using ReturnType = R;
  using ArgType = Arg;
  using ArgTypes = std::tuple<Arg>;
};

template <typename C, typename R, typename Arg>
struct ClosureTraits<R (C::*)(Arg) const> {
  using ReturnType = R;
  using ArgType = Arg;
  using ArgTypes = std::tuple<Arg>;
};

template <typename R, typename... Args>
struct ClosureTraits<R(Args...)> {
  using ReturnType = R;
  using ArgTypes = std::tuple<Args...>;
};

template <typename C, typename R, typename... Args>
struct ClosureTraits<R (C::*)(Args...)> {
  using ReturnType = R;
  using ArgTypes = std::tuple<Args...>;
};

template <typename C, typename R, typename... Args>
struct ClosureTraits<R (C::*)(Args...) const> {
  using ReturnType = R;
  using ArgTypes = std::tuple<Args...>;
};

static_assert(std::is_same_v<ClosureTraits<void()>::ReturnType, void>);
static_assert(std::is_same_v<ClosureTraits<void()>::ArgType, void>);
static_assert(std::is_same_v<ClosureTraits<void()>::ArgTypes, std::tuple<>>);
static_assert(std::is_same_v<ClosureTraits<void(void)>::ArgType, void>);
static_assert(std::is_same_v<ClosureTraits<void(void)>::ArgTypes, std::tuple<>>);  // FIXME: NOT std::tuple<void>
static_assert(std::is_same_v<ClosureTraits<void(int)>::ArgType, int>);
static_assert(std::is_same_v<ClosureTraits<void(int)>::ArgTypes, std::tuple<int>>);
static_assert(std::is_same_v<ClosureTraits<void(int, bool)>::ArgTypes, std::tuple<int, bool>>);

template <typename T>
struct Flatten {
  using type = T;
};

template <>
struct Flatten<std::tuple<>> {
  using type = void;
};

template <typename T>
struct Flatten<std::tuple<T>> {
  using type = T;
};

template <typename T>
using Flatten_t = typename Flatten<T>::type;

static_assert(std::is_same_v<Flatten_t<std::tuple<>>, void>);
static_assert(std::is_same_v<Flatten_t<std::tuple<bool>>, bool>);
static_assert(std::is_same_v<Flatten_t<std::tuple<bool, int>>, std::tuple<bool, int>>);
static_assert(std::is_same_v<Flatten_t<std::tuple<std::tuple<>>>, std::tuple<>>);

struct MakeReadyFutureTag {};
struct MakeExceptionFutureTag {};

// Only for std::apply use.
template <typename... T>
Future<T...> MakeReadyFuture0(T&&... val) {
  return MakeReadyFuture<T...>(std::forward<T>(val)...);
}

template <typename... T>
class Continuation {
 public:
  virtual void Ready(std::tuple<T...>&& val) = 0;
  virtual void Fail(std::exception_ptr&& e) = 0;

 protected:
  template <typename Callback, typename V, typename PromiseType>
  static void Resolve(Callback&& cb, V&& v, PromiseType&& pr) {
    using R = typename ClosureTraits<Callback>::ReturnType;
    static_assert(std::is_same_v<PromiseType, typename Futurized<R>::PromiseType>);

    if constexpr (std::is_void_v<R>) {
      try {
        std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        pr.SetValue();  // FIMXE: noexcept?
      } catch (...) {
        pr.SetException(std::current_exception());
      }
    } else if constexpr (!IsFuture_v<R>) {
      try {
        auto r = std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        pr.SetValue(std::move(r));  // FIXME: noexcept?
      } catch (...) {
        pr.SetException(std::current_exception());
      }
    } else {
      try {
        auto r = std::apply(std::forward<Callback>(cb), std::forward<V>(v));
        r.Then([pr = std::forward<PromiseType>(pr)](Futurized<R>&& ft) mutable {  // FIXME: noexcept?
          if (ft.IsReady()) {
            if constexpr (std::is_same_v<Futurized<R>, Future<>>)
              pr.SetValue();
            else
              pr.SetValue(ft.GetValue());
          } else
            pr.SetException(ft.GetException());
        });
      } catch (...) {
        pr.SetException(std::current_exception());
      }
    }
  }
};

template <typename Callback, typename... T>
class ContinuationWithValue : public Continuation<T...> {
  using R = typename ClosureTraits<Callback>::ReturnType;
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
  ContinuationWithValue(Callback&& cb, PromiseType&& pr) : cb_(std::forward<Callback>(cb)), pr_(std::move(pr)) {}
};

template <typename Callback, typename... T>
class ContinuationWithFuture : public Continuation<T...> {
  using R = typename ClosureTraits<Callback>::ReturnType;
  using PromiseType = typename Futurized<R>::PromiseType;

  void Ready(std::tuple<T...>&& val) override {
    auto ft = std::apply(MakeReadyFuture0<T...>, std::forward<std::tuple<T...>>(std::move(val)));
    Continuation<T...>::Resolve(std::move(cb_), std::forward_as_tuple(std::move(ft)), std::move(pr_));
  }

  void Fail(std::exception_ptr&& e) override {
    auto ft = MakeExceptionFuture<T...>(std::move(e));
    Continuation<T...>::Resolve(std::move(cb_), std::forward_as_tuple(std::move(ft)), std::move(pr_));
  }

  Callback cb_;
  PromiseType pr_;

 public:
  ContinuationWithFuture(Callback&& cb, PromiseType&& pr) : cb_(std::forward<Callback>(cb)), pr_(std::move(pr)) {}
};

template <typename... T>
class FutureState {
  static_assert(sizeof...(T) == 0 || IsDefaultConstructible_v<T...>, "Types should be default-constructible.");

 public:
  template <typename... U>
  void SetValue(U&&... val) {
    if (state_ != 0) throw std::runtime_error("unexpected state");
    state_ = 1;
    // Ref: https://en.cppreference.com/w/cpp/language/dependent_name
    result_.template emplace<0>(std::forward<U>(val)...);
    TrySchedule();
  }

  void SetValue(std::tuple<T...>&& val) {
    if (state_ != 0) throw std::runtime_error("unexpected state");
    state_ = 1;
    result_.template emplace<0>(std::move(val));
    TrySchedule();
  }

  void SetValue(const std::tuple<T...>& val) {
    if (state_ != 0) throw std::runtime_error("unexpected state");
    state_ = 1;
    result_.template emplace<0>(val);
    TrySchedule();
  }

  void SetException(std::exception_ptr&& e) {
    if (state_ != 0) throw std::runtime_error("unexpected state");
    state_ = 2;
    result_.template emplace<1>(std::make_exception_ptr(std::move(e)));
    TrySchedule();
  }

  template <typename Callback>
  auto SetCallback(Callback&& cb) {
    if (cb_) throw std::runtime_error("unexpected state");
    if (result_got_) throw std::runtime_error("result got");

    using R = typename ClosureTraits<Callback>::ReturnType;

    typename Futurized<R>::PromiseType pr;
    auto ft = pr.GetFuture();

    using ArgTuple = typename ClosureTraits<Callback>::ArgTypes;

    // TODO: To be more friendly, continuation type limit: lambda, args...

    static_assert(!IsNonConstLvalueReference_v<ArgTuple>,
                  "Continuation's parameters are NOT allowed to be non-const lvalue reference.");

    using RawArgs = RemoveCVRef_t<ArgTuple>;
    using Expected1 = std::tuple<T...>;
    using Expected2 = std::tuple<Future<T...>>;

    if constexpr (std::is_same_v<RawArgs, Expected1>) {
      cb_ = std::make_unique<ContinuationWithValue<Callback, T...>>(std::forward<Callback>(cb), std::move(pr));
    } else {
      static_assert(std::is_same_v<RawArgs, Expected2>, "Continuation's parameters' types are invalid.");
      cb_ = std::make_unique<ContinuationWithFuture<Callback, T...>>(std::forward<Callback>(cb), std::move(pr));
    }

    TrySchedule();
    return ft;
  }

  int GetState() const { return state_; }

  auto&& GetResult() {
    if (result_got_) throw std::runtime_error("result got");

    result_got_ = true;
    return std::move(result_);
  }

 private:
  void TrySchedule() {
    if (!cb_) return;

    if (state_ == 1) {
      cb_->Ready(std::get<0>(std::move(result_)));
    } else if (state_ == 2) {
      cb_->Fail(std::get<1>(std::move(result_)));
    }
  }

 private:
  int state_{0};  // 0: unresolved, 1: ready, 2: failed

  std::variant<std::tuple<T...>, std::exception_ptr> result_;

  bool result_got_{false};

  // Continuation's type info is unknown until callback set(i.e. runtime), but it's necessary while compiling
  // resolving code(i.e. SetValue, SetException), therefore we can only rely on runtime polymorphism.
  std::unique_ptr<Continuation<T...>> cb_;
};

}  // namespace details

// User should explicitly assign template argument, for the sake of avoiding unexpected template argument deduction(e.g.
// reference type), at the other hand, we want to keep arguments' type, so we can rely on the arguments deduction with
// U&&.
//
// FIXME: Is it valid to declare multiple template param pack? Reference to seastar::make_ready_future.
// Ref: https://stackoverflow.com/questions/9831501/how-can-i-have-multiple-parameter-packs-in-a-variadic-template
template <typename... T, typename... U>
Future<T...> MakeReadyFuture(U&&... val) {
  return Future<T...>(details::MakeReadyFutureTag{}, std::forward<U>(val)...);
}

template <typename... T>
Future<T...> MakeExceptionFuture(std::exception_ptr&& e) {
  return Future<T...>(details::MakeExceptionFutureTag{}, std::move(e));
}

// FIXME: Is it valid to make template param pack before others?
template <typename... T, typename E>
Future<T...> MakeExceptionFuture(E&& e) {
  static_assert(std::is_base_of_v<std::exception, E>, "Exception should be derived from std::exception.");
  return Future<T...>(details::MakeExceptionFutureTag{}, std::make_exception_ptr(std::forward<E>(e)));
}

template <typename... T>
class Promise;

template <typename... T>
class Future {
  static_assert(details::IsNotVoid_v<T...>,
                "Future's template arguments are NOT allowed to be void, use Future<> instead of Future<void>.");

  static_assert(details::IsNotReference_v<T...>, "Future's template arguments are NOT allowed to be reference.");

 public:
  using PromiseType = Promise<T...>;

  Future(Future&& other) = default;
  Future& operator=(Future&& other) = default;

  template <typename Callback>
  auto Then(Callback&& cb) {
    if (!state_) throw std::runtime_error("broken future");

    return state_->SetCallback(std::forward<Callback>(cb));
  }

  bool IsReady() const {
    if (!state_) throw std::runtime_error("broken future");
    return state_->GetState() == 1;
  }

  bool IsFailed() const {
    if (!state_) throw std::runtime_error("broken future");
    return state_->GetState() == 2;
  }

  bool IsResolved() const {
    if (!state_) throw std::runtime_error("broken future");
    return state_->GetState() != 0;
  }

  template <typename = std::enable_if_t<sizeof...(T) != 0>>
  auto GetValue() {  // FIXME: return type?
    assert(IsReady());
    auto&& t = std::get<0>(state_->GetResult());
    if constexpr (sizeof...(T) == 1)
      return std::get<0>(std::move(t));
    else
      return t;
  }

  template <std::size_t I, typename = std::enable_if_t<sizeof...(T) != 0>>
  auto GetValue() {  // FIXME: return type?
    static_assert(I < sizeof...(T), "Index out of range.");
    assert(IsReady());
    auto&& t = std::get<0>(state_->GetResult());
    return std::get<I>(std::move(t));
  }

  auto GetException() {  // FIXME: return type? how to unwrap std::exception_ptr
    assert(IsFailed());
    return std::get<1>(state_->GetResult());
  }

  // Only used in `Promise<Future<X...>, Y...>`/`Future<Future<X...>, Y...>`, because `FutureState<Future<X...>, Y...>`
  // needs to be default-constructible.
  Future() {}

 private:
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;

  Future(std::shared_ptr<details::FutureState<T...>> state) : state_(std::move(state)) {}

  template <typename... U>
  Future(details::MakeReadyFutureTag, U&&... val) {
    state_ = std::make_shared<details::FutureState<T...>>();
    state_->SetValue(std::forward<U>(val)...);
  }

  Future(details::MakeExceptionFutureTag, std::exception_ptr&& e) {
    state_ = std::make_shared<details::FutureState<T...>>();
    state_->template SetException(std::move(e));
  }

  template <typename... U, typename... V>
  friend Future<U...> MakeReadyFuture(V&&...);

  template <typename... U>
  friend Future<U...> MakeExceptionFuture(std::exception_ptr&&);

  template <typename... U, typename E>
  friend Future<U...> MakeExceptionFuture(E&&);

  friend class Promise<T...>;

 private:
  std::shared_ptr<details::FutureState<T...>> state_;
};

template <typename... T>
class Promise {
  static_assert(details::IsNotVoid_v<T...>,
                "Promise's template arguments are NOT allowed to be void, use Promise<> instead of Promise<void>.");
  static_assert(details::IsNotReference_v<T...>, "Promise's template arguments are NOT allowed to be reference.");

 public:
  Promise() : state_(std::make_shared<details::FutureState<T...>>()) {}

  Promise(Promise&& other) = default;
  Promise& operator=(Promise&& other) = default;

  Future<T...> GetFuture() {
    if (!state_) throw std::runtime_error("broken promise");
    if (future_got_) throw std::runtime_error("future got");
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
    std::apply([this](T&&... val) { SetValue(std::forward<T>(val)...); }, std::move(val));
#endif
  }

  // General version, `const &` may be bound to any value.
  void SetValue(const std::tuple<T...>& val) { state_->SetValue(val); }

  void SetException(std::exception_ptr&& e) { state_->SetException(std::move(e)); }

  template <typename E>
  void SetException(E&& e) {
    state_->SetException(std::make_exception_ptr(std::forward<E>(e)));
  }

 private:
  Promise(const Promise&) = delete;
  Promise& operator=(const Promise&) = delete;

 private:
  std::shared_ptr<details::FutureState<T...>> state_;
  bool future_got_{false};
};

// static_assert(details::IsDefaultConstructible_v<Future<>>);  // FIXME: false?
static_assert(details::IsDefaultConstructible_v<Promise<>>);
static_assert(details::IsDefaultConstructible_v<Future<bool>>);
static_assert(details::IsDefaultConstructible_v<Promise<int>>);

}  // namespace mfuture