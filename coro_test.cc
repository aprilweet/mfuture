#include <coroutine>
#include <iostream>

struct Awaiter {
  ~Awaiter() { std::cout << "~Awaiter" << std::endl; }
  bool await_ready() noexcept {
    std::cout << "await_ready" << std::endl;
    return true;
  }
  bool await_suspend(std::coroutine_handle<> h) noexcept {
    std::cout << "await_suspend" << std::endl;
    return true;
  }
  void await_resume() noexcept { std::cout << "await_resume" << std::endl; }
};

struct Task {
  std::coroutine_handle<> coro;

  ~Task() {
    std::cout << "~Task" << std::endl;
    // coro.destroy();
  }

  struct promise_type {
    ~promise_type() { std::cout << "~promise_type" << std::endl; }
    Task get_return_object() {
      std::cout << "get_return_object" << std::endl;
      return {std::coroutine_handle<promise_type>::from_promise(*this)};
    };
    std::suspend_never initial_suspend() {
      std::cout << "initial_suspend" << std::endl;
      return {};
    }
    Awaiter final_suspend() noexcept {
      std::cout << "final_suspend" << std::endl;
      return {};
    }
    void return_void() { std::cout << "return_void" << std::endl; }
    void unhandled_exception() {
      std::cout << "unhandled_exception" << std::endl;
    }
  };
};

Task foo() {
  std::cout << "foo started" << std::endl;
  co_await Awaiter{};
  std::cout << "foo resumed" << std::endl;
  co_return;
}

int main() {
  std::cout << "main started" << std::endl;
  auto t = foo();
  std::cout << "main continued 1" << std::endl;
  // t.coro.resume();
  std::cout << "main continued 2" << std::endl;
  return 0;
}
