#include "nfuture.h"

#include <iostream>

#include "gtest/gtest.h"

using namespace nfuture;

TEST(Future, basic0) {
  { Future<> future; }
  { Future<void> future; }
  { Future<float> future; }
  { Future<int, bool> future; }
}

TEST(Future, basic1) {
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
    auto future =
        MakeExceptionalFuture<>(std::make_exception_ptr("error")).Then([&]() {
          // Never reach here.
          ++counter;
          return true;
        });

    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), const char *);
    // FutureState is not moved, so we can still access it.

    // FIXME(quintonwang): What's the type of ThenWrap?
    auto future2 = future.ThenWrap([&](Future<bool> &&ft) {
      EXPECT_TRUE(ft.Failed());
      EXPECT_THROW(std::rethrow_exception(ft.Exception()), const char *);
      ++counter;
      return true;
    });
    EXPECT_TRUE(future2.Ready());
    EXPECT_TRUE(future2.Value<0>());
  }

  ASSERT_EQ(counter, 5);
}

TEST(Future, basic2) {
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
  }
  ASSERT_EQ(counter, 3);
}

TEST(Promise, basic0) {
  {
    Promise<> promise;
    promise.GetFuture();
  }
  {
    Promise<void> promise;
    promise.GetFuture();
  }
  {
    Promise<float> promise;
    promise.GetFuture();
  }
  {
    Promise<int, bool> promise;
    promise.GetFuture();
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
    promise.SetException(std::make_exception_ptr(0.1f));
    auto future = promise.GetFuture().Then([&](int val) {
      // Never reach here.
      ++counter;
    });
    EXPECT_TRUE(future.Failed());
    EXPECT_THROW(std::rethrow_exception(future.Exception()), float);
  }
  ASSERT_EQ(counter, 1);
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