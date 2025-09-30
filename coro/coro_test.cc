#include "coro/coro.h"

#include "gtest/gtest.h"

using namespace fabric;

TEST(Coro, basic) {
  bool done = false;
  auto lambda = [&]() -> Coro {
    done = true;
    co_return;  // if missing, not a coroutine, undefined behavior
  };
  auto coro = lambda();
  ASSERT_TRUE(done);
}

TEST(Coro, future) {
  bool done = false;
  auto lambda = [&]() -> Coro {
    co_await MakeReadyFuture();
    done = true;
    // implicit co_return;
  };
  auto coro = lambda();
  ASSERT_TRUE(done);
}

TEST(Coro, future2) {
  bool done = false;
  Promise<> promise;
  auto lambda = [&]() -> Coro {
    co_await promise.GetFuture();
    done = true;
  };
  auto coro = lambda();
  ASSERT_FALSE(done);
  promise.SetValue();
  ASSERT_TRUE(done);
}
