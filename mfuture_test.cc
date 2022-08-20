#include "mfuture.h"

#include <cstring>
#include <iostream>

#include "gtest/gtest.h"

using namespace mfuture;

TEST(Future, basic0) {
  EXPECT_TRUE(MakeReadyFuture<>().IsReady());
  EXPECT_FALSE(MakeReadyFuture<>().IsFailed());
  EXPECT_TRUE(MakeReadyFuture<>().IsResolved());

  EXPECT_FALSE(MakeExceptionFuture<>(std::runtime_error("test")).IsReady());
  EXPECT_TRUE(MakeExceptionFuture<>(std::runtime_error("test")).IsFailed());
  EXPECT_TRUE(MakeExceptionFuture<>(std::runtime_error("test")).IsResolved());
  // EXPECT_EQ(0, strcmp(MakeExceptionFuture<>(std::runtime_error("test")).GetException(), "test"));
}

TEST(Future, basic1) {
  EXPECT_TRUE(MakeReadyFuture<bool>(false).IsReady());
  EXPECT_FALSE(MakeReadyFuture<int>(1).IsFailed());
  EXPECT_TRUE(MakeReadyFuture<void*>(nullptr).IsResolved());
  EXPECT_TRUE(MakeReadyFuture<bool>(true).GetValue());
  EXPECT_TRUE(MakeReadyFuture<bool>(true).GetValue<0>());

  EXPECT_FALSE(MakeExceptionFuture<bool>(std::runtime_error("test")).IsReady());
  EXPECT_TRUE(MakeExceptionFuture<int>(std::runtime_error("test")).IsFailed());
  EXPECT_TRUE(MakeExceptionFuture<void*>(std::runtime_error("test")).IsResolved());
  // EXPECT_EQ(0, strcmp(MakeExceptionFuture<>(std::runtime_error("test")).GetException(), "test"));

  int a = 1;
  EXPECT_TRUE(MakeReadyFuture<int>(a).IsReady());
  const bool b = true;
  EXPECT_TRUE(MakeReadyFuture<int>(b).IsReady());
}

TEST(Future, basic2) {
  // Ref: https://stackoverflow.com/questions/4496842/pass-method-with-template-arguments-to-a-macro
  EXPECT_TRUE((MakeReadyFuture<bool, int>(false, 2).IsReady()));
  EXPECT_FALSE((MakeReadyFuture<bool, int>(false, 2).IsFailed()));
  EXPECT_TRUE((MakeReadyFuture<bool, int>(false, 2).IsResolved()));
  EXPECT_TRUE((MakeReadyFuture<bool, int>(true, 2).GetValue<0>()));
  EXPECT_EQ((MakeReadyFuture<bool, int>(true, 2).GetValue<1>()), 2);
  EXPECT_EQ((MakeReadyFuture<bool, int>(true, 2).GetValue()), std::make_tuple(true, 2));

  EXPECT_FALSE((MakeExceptionFuture<bool, int>(std::runtime_error("test")).IsReady()));
  EXPECT_TRUE((MakeExceptionFuture<bool, int>(std::runtime_error("test")).IsFailed()));
  EXPECT_TRUE((MakeExceptionFuture<bool, int>(std::runtime_error("test")).IsResolved()));
  // EXPECT_EQ(0, strcmp(MakeExceptionFuture<bool, int>(std::runtime_error("test")).GetException(), "test"));

  int a = 1;
  const bool b = true;
  EXPECT_TRUE((MakeReadyFuture<int, bool>(a, true)).IsReady());
  EXPECT_TRUE((MakeReadyFuture<bool, bool>(b, false)).IsReady());
}

TEST(Promise, basic0) {
  {
    Promise<> pr;
    auto ft = pr.GetFuture();

    EXPECT_FALSE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());

    pr.SetValue();
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_TRUE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());
  }
  {
    Promise<> pr;
    auto ft = pr.GetFuture();

    pr.SetException(std::runtime_error("test"));
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_TRUE(ft.IsFailed());
    // EXPECT_EQ(0, strcmp(ft.GetException(), "test"));
  }
  {
    Promise<> pr;
    pr.SetValue(std::make_tuple());
  }
  {
    Promise<> pr;
    std::tuple<> a;
    pr.SetValue(a);
  }
  {
    Promise<> pr;
    const std::tuple<> a;
    pr.SetValue(a);
  }
}

TEST(Promise, basic1) {
  {
    Promise<bool> pr;
    auto ft = pr.GetFuture();

    EXPECT_FALSE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());

    pr.SetValue(true);
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_TRUE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());
    EXPECT_TRUE(ft.GetValue());
  }
  {
    Promise<float> pr;
    auto ft = pr.GetFuture();

    pr.SetException(std::runtime_error("test"));
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_TRUE(ft.IsFailed());
    // EXPECT_EQ(0, strcmp(ft.GetException(), "test"));
  }
  {
    Promise<bool> pr;
    bool a = false;
    pr.SetValue(a);
  }
  {
    Promise<bool> pr;
    const bool a = false;
    pr.SetValue(a);
  }
  {
    Promise<int> pr;
    pr.SetValue(std::make_tuple(1));
  }
}

TEST(Promise, basic2) {
  {
    Promise<std::nullptr_t, float> pr;
    auto ft = pr.GetFuture();

    EXPECT_FALSE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());

    pr.SetValue(nullptr, 3.14);
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_TRUE(ft.IsReady());
    EXPECT_FALSE(ft.IsFailed());
    EXPECT_EQ(ft.GetValue(), std::make_tuple(nullptr, 3.14f));  // 3.14f != 3.14
  }
  {
    Promise<void*, bool> pr;
    auto ft = pr.GetFuture();

    pr.SetException(std::runtime_error("test"));
    EXPECT_TRUE(ft.IsResolved());
    EXPECT_FALSE(ft.IsReady());
    EXPECT_TRUE(ft.IsFailed());
    // EXPECT_EQ(0, strcmp(ft.GetException(), "test"));
  }
  {
    Promise<bool, int> pr;
    bool a = false;
    int b = 1;
    pr.SetValue(a, b);
  }
  {
    Promise<bool, int> pr;
    bool a = false;
    pr.SetValue(a, 1);
  }
  {
    Promise<float, int> pr;
    long a = 1;
    pr.SetValue(std::make_tuple(a, a));
  }
}

TEST(Future, Then0) {
  {
    auto ft = MakeReadyFuture<>().Then([]() {});
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeReadyFuture<>().Then([]() { return true; });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_TRUE(ft.GetValue());
  }
  {
    auto ft = MakeReadyFuture<>().Then([]() { return MakeReadyFuture<>(); });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeReadyFuture<>().Then([]() { return MakeExceptionFuture<>(std::runtime_error("test")); });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<>().Then([]() { throw std::runtime_error("test"); });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<>().Then([](Future<>&& ft) { EXPECT_TRUE(ft.IsReady()); });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft =
        MakeExceptionFuture<>(std::runtime_error("test")).Then([](const Future<>& ft) { EXPECT_TRUE(ft.IsFailed()); });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeExceptionFuture<>(std::runtime_error("test")).Then([](Future<> ft) {
      EXPECT_TRUE(ft.IsFailed());
      std::rethrow_exception(ft.GetException());
    });
    EXPECT_TRUE(ft.IsFailed());
  }
}

TEST(Future, Then1) {
  {
    auto ft = MakeReadyFuture<void*>(nullptr).Then([](void* p) { return p; });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_EQ(ft.GetValue(), nullptr);
  }
  {
    auto ft = MakeReadyFuture<bool>(true).Then([](const bool& b) { return !b; });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_EQ(ft.GetValue(), false);
  }
  {
    auto ft = MakeReadyFuture<int>(0).Then([](int&& i) { return MakeReadyFuture<bool>(i == 0); });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_TRUE(ft.GetValue());
  }
  {
    auto ft = MakeReadyFuture<std::nullptr_t>(nullptr).Then(
        [](std::nullptr_t&&) { return MakeExceptionFuture<>(std::runtime_error("test")); });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<float>(3.14f).Then([](float) { throw std::runtime_error("test"); });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<bool>(true).Then([](Future<bool>&& ft) {
      EXPECT_TRUE(ft.IsReady());
      EXPECT_TRUE(ft.GetValue());
    });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeExceptionFuture<float>(std::runtime_error("test")).Then([](const Future<float>& ft) {
      EXPECT_TRUE(ft.IsFailed());
    });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeExceptionFuture<std::function<void()>>(std::runtime_error("test"))
                  .Then([](Future<std::function<void()>> ft) {
                    EXPECT_TRUE(ft.IsFailed());
                    std::rethrow_exception(ft.GetException());
                  });
    EXPECT_TRUE(ft.IsFailed());
  }
}

TEST(Future, Then2) {
  {
    auto ft = MakeReadyFuture<void*, bool>(nullptr, false).Then([](void* p, bool) { return p; });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_EQ(ft.GetValue(), nullptr);
  }
  {
    auto ft = MakeReadyFuture<bool, long>(true, 3).Then(
        [](const bool& b, long&& l) { return MakeReadyFuture<long, bool>(l + 1, !b); });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_EQ(ft.GetValue(), std::make_tuple(4, false));
  }
  {
    auto ft =
        MakeReadyFuture<int, int>(0, 1).Then([](int&& i, int j) { return MakeReadyFuture<int, bool>(j, i == 0); });
    EXPECT_TRUE(ft.IsReady());
    EXPECT_EQ(ft.GetValue(), std::make_tuple(1, true));
  }
  {
    auto ft = MakeReadyFuture<std::nullptr_t, float>(nullptr, 3.14).Then([](std::nullptr_t&&, float) {
      return MakeExceptionFuture<>(std::runtime_error("test"));
    });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<float, long>(3.14f, 10).Then([](float, long) { throw std::runtime_error("test"); });
    EXPECT_TRUE(ft.IsFailed());
  }
  {
    auto ft = MakeReadyFuture<bool, bool>(true, false).Then([](Future<bool, bool>&& ft) {
      EXPECT_TRUE(ft.IsReady());
      EXPECT_EQ(ft.GetValue(), std::make_tuple(true, false));
    });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeExceptionFuture<float, void*>(std::runtime_error("test")).Then([](const Future<float, void*>& ft) {
      EXPECT_TRUE(ft.IsFailed());
    });
    EXPECT_TRUE(ft.IsReady());
  }
  {
    auto ft = MakeExceptionFuture<std::function<void()>, bool>(std::runtime_error("test"))
                  .Then([](Future<std::function<void()>, bool> ft) {
                    EXPECT_TRUE(ft.IsFailed());
                    std::rethrow_exception(ft.GetException());
                  });
    EXPECT_TRUE(ft.IsFailed());
  }
}

TEST(Future, Chain) {
  int i = 10;
  Promise<> pr;
  auto ft = pr.GetFuture()
                .Then([&i]() {
                  --i;
                  return i;
                })
                .Then([](int i) { return i == 0; })
                .Then([](Future<bool>&& ft) {
                  EXPECT_TRUE(ft.IsReady());
                  EXPECT_FALSE(ft.GetValue());
                  throw std::runtime_error("test");
                })
                .Then([&i]() {
                  i = 0;
                  EXPECT_TRUE(false);  // never reach here.
                  return true;
                })
                .Then([&i](const Future<bool>& ft) {
                  EXPECT_TRUE(ft.IsFailed());
                  return i;
                });

  EXPECT_EQ(i, 10);
  EXPECT_FALSE(ft.IsResolved());

  pr.SetValue();
  EXPECT_TRUE(ft.IsReady());
  EXPECT_EQ(ft.GetValue(), 9);
  EXPECT_EQ(i, 9);
}

TEST(Future, testtest) {
  Promise<Future<>> pr;
  Future<Future<>> ft;
}

// TODO: Future with: Promise, Future, void, std::void_t, std::tuple, std::exception, std::exception_ptr