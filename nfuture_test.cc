#include "nfuture.h"

#include <iostream>

#include "gtest/gtest.h"

using namespace nfuture;

TEST(Future, basic0) {
  int counter = 0;
  {
    auto future = MakeReadyFuture<float>(0.1f)
                      .Then([&](float val) {
                        EXPECT_EQ(val, 0.1f);
                        ++counter;
                        return 1;
                      })
                      .ThenWrap([&](Future<int> ft) {
                        EXPECT_TRUE(ft.Ready());
                        EXPECT_EQ(ft.Value<0>(), 1);
                        ++counter;
                        return true;
                      });
    EXPECT_TRUE(future.Ready());
    EXPECT_TRUE(future.Value<0>());
  }
  {
    auto future = MakeReadyFuture<float>(0.1f)
                      .Then([&](float &&val) {
                        EXPECT_EQ(val, 0.1f);
                        ++counter;
                        return 1;
                      })
                      .ThenWrap([&](Future<int> &&ft) {
                        EXPECT_TRUE(ft.Ready());
                        EXPECT_EQ(ft.Value<0>(), 1);
                        ++counter;
                        return true;
                      });
    EXPECT_TRUE(future.Ready());
    EXPECT_TRUE(future.Value<0>());
  }
  {
    auto future = MakeExceptionalFuture<>(std::make_exception_ptr("error"))
                      .Then([&]() {
                        // Never reach here.
                        ++counter;
                        return true;
                      })
                      .ThenWrap([&](Future<bool> &&ft) {
                        EXPECT_TRUE(ft.Failed());
                        EXPECT_THROW(std::rethrow_exception(ft.Exception()),
                                     const char *);
                        ++counter;
                        return true;
                      });
    EXPECT_TRUE(future.Ready());
    EXPECT_TRUE(future.Value<0>());
  }

  ASSERT_EQ(counter, 5);
}

TEST(Future, basic1) {
  int counter = 0;
  {
    auto future =
        MakeReadyFuture<bool, int>(true, 1)
            .Then([&](bool val, int &&val2) {
              EXPECT_TRUE(val);
              EXPECT_EQ(val2, 1);
              ++counter;
              return MakeReadyFuture<int>(1);
            })
            .ThenWrap([&](Future<int> ft) {
              EXPECT_TRUE(ft.Ready());
              EXPECT_EQ(ft.Value<0>(), 1);
              ++counter;
              return MakeExceptionalFuture<int>(std::make_exception_ptr(0.1f));
            })
            .Then([&](int val) {
              // Never reach here.
              ++counter;
              return true;
            })
            .ThenWrap([&](Future<bool> ft) {
              EXPECT_TRUE(ft.Failed());
              EXPECT_THROW(std::rethrow_exception(ft.Exception()), float);
              ++counter;
              return true;
            });
    EXPECT_TRUE(future.Ready());
  }
  ASSERT_EQ(counter, 3);
}

TEST(Promise, basic0) {
  {
    Promise<> promise;
    promise.GetFuture().Ignore();
  }
  {
    Promise<void> promise;
    promise.GetFuture().Ignore();
  }
  {
    Promise<float> promise;
    promise.GetFuture().Ignore();
  }
  {
    Promise<int, bool> promise;
    promise.GetFuture().Ignore();
  }
}

TEST(Promise, basic1) {
  int counter = 0;
  {
    Promise<> promise;
    auto future = promise.GetFuture().Then([&]() { ++counter; });
    EXPECT_FALSE(future.Available());
    promise.SetValue();
    EXPECT_TRUE(future.Ready());
    ASSERT_EQ(counter, 1);
  }
  {
    Promise<int> promise;
    auto future = promise.GetFuture().Then([&](int val) {
      // Never reach here.
      ++counter;
    });
    EXPECT_FALSE(future.Available());
    promise.SetException(std::make_exception_ptr(0.1f));
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
  ASSERT_EQ(counter, 1);
}

TEST(DoUntil, failed) {
  int counter = 0;
  auto future =
      DoUntil([]() { return false; },
              [&counter]() {
                ++counter;
                return MakeExceptionalFuture<>(std::make_exception_ptr("stop"));
              });
  EXPECT_TRUE(future.Failed());
  EXPECT_THROW(std::rethrow_exception(future.Exception()), const char *);
  EXPECT_EQ(counter, 1);
}

TEST(DoUntil, pending_failed1) {
  int counter = 0;
  Promise<> promise;
  auto future = DoUntil([&counter]() { return counter == 1; },
                        [&counter, &promise]() {
                          ++counter;
                          return promise.GetFuture();
                        });
  ASSERT_FALSE(future.Available());
  promise.SetException(std::make_exception_ptr("stop"));

  ASSERT_TRUE(future.Failed());
  ASSERT_THROW(std::rethrow_exception(future.Exception()), const char *);
  ASSERT_EQ(counter, 1);
}

TEST(DoUntil, pending_failed2) {
  int counter = 0;
  Promise<> promise;
  auto future = DoUntil(
      []() { return false; },
      [&counter, &promise]() {
        if (counter == 0) {
          ++counter;
          return promise.GetFuture();
        } else {
          ++counter;
          return MakeExceptionalFuture<>(std::make_exception_ptr("quit"));
        }
      });
  ASSERT_FALSE(future.Available());
  promise.SetValue();

  ASSERT_TRUE(future.Failed());
  ASSERT_THROW(std::rethrow_exception(future.Exception()), const char *);
  ASSERT_EQ(counter, 2);
}

constexpr int kTimes = 1000000;

TEST(perf, mark) {
  int counter = 0;
  auto do_until = [](auto &&stop, auto &&func) {
    while (!stop()) {
      func();
    }
  };

  {
    auto stop = [n = kTimes]() mutable { return n-- == 0; };
    auto func = [&]() { ++counter; };
    do_until(std::function([&]() mutable { return stop(); }),
             std::function([&]() { func(); }));
  }

  {  // This will be optimized to NOOP.
     // do_until([n = kTimes]() mutable { return n-- == 0; }, [&]() {
     // ++counter;
     // });
  }

  ASSERT_EQ(counter, kTimes);
}

TEST(perf, ready) {
  int counter = 0;
  auto future = DoUntil([n = kTimes]() mutable { return n-- == 0; },
                        [&]() {
                          ++counter;
                          return MakeReadyFuture<>();
                        });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(perf, unready) {
  int counter = 0;
  Promise<> *last_promise = nullptr;
  auto future = DoUntil(
      [&, n = kTimes]() mutable {
        if (last_promise) {
          delete last_promise;
          last_promise = nullptr;
        }
        return n-- == 0;
      },
      [&]() {
        EXPECT_EQ(last_promise, nullptr);
        last_promise = new Promise<>();
        ++counter;
        return last_promise->GetFuture();
      });

  while (last_promise) {
    // There is no underlying scheduler, so we can drive it in this way.
    last_promise->SetValue();
  }

  EXPECT_TRUE(future.Ready());
  EXPECT_EQ(counter, kTimes);
}

TEST(perf, ready_then1) {
  ASSERT_EQ(kTimes % 10, 0);

  int counter = 0;
  auto future = DoUntil([n = kTimes / 10]() mutable { return n-- == 0; },
                        [&]() {
                          auto future = MakeReadyFuture<>();
                          for (int i = 0; i < 10; ++i) {
                            future = future.Then([&]() { ++counter; });
                          }
                          return future;
                        });

  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(perf, ready_then2) {
  ASSERT_EQ(kTimes % 10, 0);

  int counter = 0;
  auto future = DoUntil([n = kTimes / 10]() mutable { return n-- == 0; },
                        [&]() {
                          auto future = MakeReadyFuture<>();
                          for (int i = 0; i < 10; ++i) {
                            future = future.Then([&]() {
                              ++counter;
                              return MakeReadyFuture<>();
                            });
                          }
                          return future;
                        });

  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(perf, ready_then3) {
  int counter = 0;
  auto future = MakeReadyFuture<>();
  for (int i = 0; i < kTimes; ++i) {
    future = future.Then([&]() { ++counter; });
  }
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(perf, unready_then) {
  ASSERT_EQ(kTimes % 10, 0);

  int counter = 0;
  auto future = DoUntil([n = kTimes / 10]() mutable { return n-- == 0; },
                        [&]() {
                          Promise<> promise;
                          auto future = promise.GetFuture();
                          for (int i = 0; i < 10; ++i) {
                            future = future.Then([&]() { ++counter; });
                          }
                          promise.SetValue();
                          return future;
                        });

  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

#ifdef COROUTINES_ENABLED

TEST(coroutines, basic0) {
  {
    auto coro = []() -> Future<> { co_return; };
    auto future = coro();
    EXPECT_TRUE(future.Ready());
  }
  {
    auto coro = []() -> Future<int> { co_return 1; };
    auto future = coro();
    EXPECT_TRUE(future.Ready());
    EXPECT_EQ(future.Value<0>(), 1);
  }
  {
    auto coro = []() -> Future<bool, char> {
      co_return std::make_tuple<bool, char>(true, 2);
    };
    auto future = coro();
    EXPECT_TRUE(future.Ready());
    auto [a, b] = future.Value();
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, 2);
  }
  {
    auto coro = []() -> Future<> {
      throw 0.1f;
      co_return;
    };
    auto future = coro();
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
  {
    auto coro = []() -> Future<int> {
      co_await MakeExceptionalFuture<>(std::make_exception_ptr(0.1f));
      EXPECT_TRUE(false);  // Never here.
      co_return -1;
    };
    auto future = coro();
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
}

TEST(coroutines, basic1) {
  {
    auto coro = []() -> Future<int> {
      co_await MakeReadyFuture<>();
      auto value = co_await MakeReadyFuture<bool>(true);
      EXPECT_EQ(value, true);
      auto [a, b] = co_await MakeReadyFuture<bool, char>(true, 2);
      EXPECT_EQ(a, true);
      EXPECT_EQ(b, 2);
      co_return 1;
      // If `return`, it does not compile.
      // If `co_return` without value, it does not compile.
      // If no any return, it compiles without any warning, however, it imposes
      // a runtime error later.
    };
    auto future = coro();
    EXPECT_TRUE(future.Ready());
    EXPECT_EQ(future.Value<0>(), 1);
  }
  {
    auto coro = []() -> Future<int> {
      co_await MakeReadyFuture<>();
      try {
        co_await MakeExceptionalFuture<>(std::make_exception_ptr(0.1f));
        EXPECT_TRUE(false);  // Never here.
      } catch (...) {
        EXPECT_THROW(std::rethrow_exception(std::current_exception()), float);
      }
      co_await MakeExceptionalFuture<bool, char>(std::make_exception_ptr(0.1f));
      EXPECT_TRUE(false);  // Never here.
      co_return -1;
    };
    auto future = coro();
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
}

TEST(coroutines, basic2) {
  {
    auto coro = []() -> Future<int> {
      auto coro = []() -> Future<int> { co_return 1; };
      auto future = coro();
      EXPECT_TRUE(future.Ready());
      co_return future.Value<0>();
    };
    auto future = coro();
    EXPECT_TRUE(future.Ready());
    EXPECT_EQ(future.Value<0>(), 1);
  }
  {
    auto coro = []() -> Future<int> {
      auto coro = []() -> Future<int> { throw 0.1f; };
      try {
        co_await coro();
        EXPECT_TRUE(false);  // Never here.
      } catch (...) {
        EXPECT_THROW(std::rethrow_exception(std::current_exception()), float);
      }
      co_await coro();
      EXPECT_TRUE(false);  // Never here.
      co_return -1;
    };
    auto future = coro();
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
}

TEST(coroutines, unready1) {
  {
    Promise<int> promise1;
    Promise<char> promise2;

    auto coro = [&]() -> Future<int> {
      auto value = co_await promise1.GetFuture();
      auto value2 = co_await promise2.GetFuture();
      co_return value + (int)value2;
    };

    auto future = coro();
    EXPECT_FALSE(future.Available());

    promise1.SetValue(1);
    EXPECT_FALSE(future.Available());

    promise2.SetValue(2);
    EXPECT_TRUE(future.Ready());
    EXPECT_EQ(future.Value<0>(), 3);
  }
  {
    Promise<int> promise1;
    Promise<char> promise2;

    auto coro = [&]() -> Future<int> {
      try {
        co_await promise1.GetFuture();
        EXPECT_TRUE(false);  // Never here.
      } catch (...) {
        EXPECT_THROW(std::rethrow_exception(std::current_exception()), bool);
      }
      co_await promise2.GetFuture();
      EXPECT_TRUE(false);  // Never here.
      co_return -1;
    };

    auto future = coro();
    EXPECT_FALSE(future.Available());

    promise1.SetException(std::make_exception_ptr(false));
    EXPECT_FALSE(future.Available());

    promise2.SetException(std::make_exception_ptr(false));
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), bool);
  }
}

TEST(coroutines, unready2) {
  {
    Promise<int> promise1;

    auto coro = [&]() -> Future<int> {
      Promise<int> promise2;
      auto coro = [&]() -> Future<int> {
        auto value = co_await promise1.GetFuture();
        promise2.SetValue(2);
        co_return value;
      };
      auto value1 = co_await coro();
      auto value2 = co_await promise2.GetFuture();
      co_return value1 + value2;
    };

    auto future = coro();
    EXPECT_FALSE(future.Available());

    promise1.SetValue(1);
    EXPECT_TRUE(future.Ready());
    EXPECT_EQ(future.Value<0>(), 3);
  }
  {
    Promise<int> promise1;

    auto coro = [&]() -> Future<int> {
      Promise<int> promise2;
      auto coro = [&]() -> Future<int> {
        try {
          co_await promise1.GetFuture();
          EXPECT_TRUE(false);  // Never here.
        } catch (...) {
          EXPECT_THROW(std::rethrow_exception(std::current_exception()), bool);
        }
        promise2.SetException(std::make_exception_ptr(false));
        co_return 1;
      };
      auto value1 = co_await coro();
      EXPECT_EQ(value1, 1);
      co_await promise2.GetFuture();
      EXPECT_TRUE(false);  // Never here.
      co_return -1;
    };

    auto future = coro();
    EXPECT_FALSE(future.Available());

    promise1.SetException(std::make_exception_ptr(false));
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), bool);
  }
}

TEST(coroutines_perf, ready0) {
  int counter = 0;

  auto coro = [&, n = kTimes]() mutable -> Future<> {
    while (n-- > 0) {
      ++counter;
      co_await MakeReadyFuture<>();
    }
  };

  auto future = coro();
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(coroutines_perf, ready1) {
  int counter = 0;
  auto future = DoUntil([n = kTimes]() mutable { return n-- == 0; },
                        [&]() -> Future<> {
                          ++counter;
                          co_return;
                        });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(coroutines_perf, ready2) {
  int counter = 0;

  auto coro = [&, n = kTimes]() mutable -> Future<> {
    while (n-- > 0) {
      auto coro = [&]() -> Future<> {
        ++counter;
        co_return;
      };
      co_await coro();
    }
  };

  auto future = coro();
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(coroutines_perf, unready0) {
  int counter = 0;

  Promise<> *promise = nullptr;
  auto coro = [&, n = kTimes]() mutable -> Future<> {
    while (n-- > 0) {
      ++counter;
      EXPECT_EQ(promise, nullptr);
      promise = new Promise<>();
      co_await promise->GetFuture();
      delete promise;
      promise = nullptr;
    }
  };

  auto future = coro();
  EXPECT_FALSE(future.Ready());

  while (promise) {
    promise->SetValue();
  }

  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

TEST(coroutines_perf, unready1) {
  int counter = 0;
  Promise<> *promise = nullptr;
  auto future = DoUntil(
      [&, n = kTimes]() mutable {
        if (promise) {
          delete promise;
          promise = nullptr;
        }
        return n-- == 0;
      },
      [&]() -> Future<> {
        EXPECT_EQ(promise, nullptr);
        promise = new Promise<>();
        ++counter;
        co_await promise->GetFuture();
      });

  while (promise) {
    promise->SetValue();
  }

  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);
}

#endif