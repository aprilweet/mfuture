#pragma once

#include <tuple>
#include <type_traits>

namespace internal {

template <typename T>
using Debug = typename T::print_type;

template <typename... T>
constexpr bool AlwaysFalse = false;

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

// Ref:
// https://stackoverflow.com/questions/36329826/how-to-specialize-template-class-without-parameters
// https://stackoverflow.com/questions/24108852/how-to-check-if-all-of-variadic-template-arguments-have-special-function
template <typename... T>
constexpr bool IsReference_v = false;

template <typename T>
constexpr bool IsReference_v<T> = std::is_reference_v<T>;

template <typename T, typename... U>
constexpr bool IsReference_v<T, U...> =
    std::is_reference_v<T> && IsReference_v<U...>;

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
constexpr bool IsNotReference_v<T, U...> =
    !std::is_reference_v<T> && IsNotReference_v<U...>;

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
constexpr bool IsNotVoid_v<T, U...> = IsNotVoid_v<T> && IsNotVoid_v<U...>;

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
    IsNonConstLvalueReference_v<T> && IsNonConstLvalueReference_v<U...>;

template <typename... T>
constexpr bool IsDefaultConstructible_v = false;

template <typename T, typename... U>
constexpr bool IsDefaultConstructible_v<T, U...> =
    std::is_default_constructible_v<T> && IsDefaultConstructible_v<U...>;

template <typename T>
constexpr bool IsDefaultConstructible_v<T> = std::is_default_constructible_v<T>;

static_assert(!IsDefaultConstructible_v<>);
static_assert(!IsDefaultConstructible_v<void>);
static_assert(IsDefaultConstructible_v<int>);
static_assert(IsDefaultConstructible_v<int, bool>);

template <typename T>
struct ClosureTraits
    : public ClosureTraits<decltype(&std::remove_reference_t<T>::operator())> {
};

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
static_assert(std::is_same_v<ClosureTraits<void(void)>::ArgTypes,
                             std::tuple<>>);  // FIXME: NOT std::tuple<void>
static_assert(std::is_same_v<ClosureTraits<void(int)>::ArgType, int>);
static_assert(
    std::is_same_v<ClosureTraits<void(int)>::ArgTypes, std::tuple<int>>);
static_assert(std::is_same_v<ClosureTraits<void(int, bool)>::ArgTypes,
                             std::tuple<int, bool>>);

template <class Function, class Tuple>
using ApplyResultType =
    decltype(std::apply(std::declval<Function>(), std::declval<Tuple>()));

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
static_assert(
    std::is_same_v<Flatten_t<std::tuple<bool, int>>, std::tuple<bool, int>>);
static_assert(
    std::is_same_v<Flatten_t<std::tuple<std::tuple<>>>, std::tuple<>>);

}  // namespace internal