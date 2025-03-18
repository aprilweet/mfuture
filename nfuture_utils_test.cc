#include <iostream>

#include "nfuture.h"
#include "semaphore.h"
#include "latch.h"
#include "do_until.h"
#include "do_with.h"
#include "when_all.h"

#include "gtest/gtest.h"

using namespace nfuture;

TEST(DoWith, test1) {
  auto obj = std::make_shared<bool>(true);
  auto future = DoWith(
                    [&](auto &obj) {
                      EXPECT_EQ(obj.use_count(), 1);
                      return MakeReadyFuture<>();
                    },
                    std::move(obj))
                    .Then([&]() { EXPECT_EQ(obj.use_count(), 0); });
  EXPECT_TRUE(future.Ready());
}

TEST(DoWith, test2) {
  auto obj = std::make_shared<bool>(true);
  auto future = DoWith(
                    [&](auto &obj) {
                      EXPECT_EQ(obj.use_count(), 1);
                      return MakeReadyFuture<>();
                    },
                    obj)
                    .Then([&]() { EXPECT_EQ(obj.use_count(), 1); });
  EXPECT_TRUE(future.Ready());
}

TEST(DoWith, test3) {
  bool deleted = false;
  Promise<> promise;
  auto task = [&]() {
    struct Deleter {
      bool &deleted_;
      Deleter(bool &deleted) : deleted_(deleted) {}

      void operator()(int *obj) {
        delete obj;
        deleted_ = true;
      }
    };
    auto obj = std::unique_ptr<int, Deleter>(new int(8), Deleter(deleted));
    return DoWith(
        [&](auto &obj, bool &deleted) {
          return promise.GetFuture().Then([&]() {
            EXPECT_FALSE(deleted);
            return MakeReadyFuture<int>(*obj);
          });
        },
        std::move(obj), deleted);
  };

  auto future = task();
  EXPECT_FALSE(future.Available());
  EXPECT_FALSE(deleted);

  promise.SetValue();
  EXPECT_TRUE(future.Available());
  EXPECT_TRUE(deleted);
  EXPECT_EQ(future.Value<0>(), 8);
}

TEST(DoWith, test4) {  // No DoWith, compared with test3.
  bool deleted = false;
  Promise<> promise;
  auto task = [&]() {
    struct Deleter {
      bool &deleted_;
      Deleter(bool &deleted) : deleted_(deleted) {}

      void operator()(bool *obj) {
        delete obj;
        deleted_ = true;
      }
    };
    auto obj = std::unique_ptr<bool, Deleter>(new bool(true), Deleter(deleted));
    return [&](bool &obj) {
      return promise.GetFuture().Then([&]() {
        EXPECT_TRUE(deleted);
        return MakeReadyFuture<int>(8);
      });
    }(*obj);
  };

  auto future = task();
  EXPECT_FALSE(future.Available());
  EXPECT_TRUE(deleted);

  promise.SetValue();
  EXPECT_TRUE(future.Available());
  EXPECT_EQ(future.Value<0>(), 8);
}
