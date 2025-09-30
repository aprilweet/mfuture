#include <coroutine>
#include "nfuture.h"

namespace fabric {

struct Coro {
  // Coro(Coro &&other) { handle_ = std::exchange(other.handle_, nullptr); }
  // Coro &operator=(Coro &&other) {
  //   if (this != &other) {
  //     assert(handle_ == nullptr);
  //     handle_ = std::exchange(other.handle_, nullptr);
  //   }
  //   return *this;
  // }

  // ~Coro() {
  //   if (handle_) {
  //     assert(handle_.done());
  //     handle_.destroy();
  //   }
  // }

  // void Detach() { handle_ = nullptr; }

  template <typename... T>
  struct awaiter {
    Future<T...> future_;
    bool await_ready() { return future_.Available(); }
    void await_suspend(std::coroutine_handle<> handle) {
      future_
          .ThenWrap([this, handle](Future<T...> &&future) {
            future_ = std::move(future);
            handle.resume();
          })
          .Ignore();
    }
    auto await_resume() {
      auto &&value = std::move(future_).Value();
      if constexpr (sizeof...(T) == 0)
        return;
      else if constexpr (sizeof...(T) == 1)
        return std::get<0>(std::move(value));
      else
        return std::move(value);
    }
  };

  struct promise_type {
    Coro get_return_object() {
      // return Coro(std::coroutine_handle<promise_type>::from_promise(*this));
      return Coro{};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}

    template <typename... T>
    awaiter<T...> await_transform(Future<T...> &&future) {
      return awaiter<T...>{std::move(future)};
    }
  };

 private:
  // explicit Coro(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  Coro() = default;
  Coro(const Coro &) = delete;
  Coro &operator=(const Coro &) = delete;

  // std::coroutine_handle<> handle_;
};

}  // namespace fabric