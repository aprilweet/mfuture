#include "mfuture.h"
#include "nfuture.h"

#include <iostream>
#include <functional>

#include "gtest/gtest.h"

namespace async_lambda {

std::function<void(int)> g_complete;

void Complete(int i) {
  g_complete(i);
  g_complete = nullptr;
}

void AsyncIO(std::function<void(int)>&& cb) {
  g_complete = [cb = std::move(cb)](int i) { cb(i + 1); };
}

void Step3(std::function<void(int)>&& cb) {
  AsyncIO([cb = std::move(cb)](int i) { cb(i + 1); });
}

void Step2(std::function<void(int)>&& cb) {
  Step3([cb = std::move(cb)](int i) { cb(i + 1); });
}

void Step1(std::function<void(int)>&& cb) {
  Step2([cb = std::move(cb)](int i) { cb(i + 1); });
}

void Run(std::function<void(int)>&& cb) {
  Step1([cb = std::move(cb)](int i) { cb(i + 1); });
}

}  // namespace async_lambda

namespace async_task {

struct Task {
  std::function<void(int)> cb;
  std::function<void(int)> step1;
  std::function<void(int)> step2;
  std::function<void(int)> step3;
  std::function<void(int)> complele;
};

std::unique_ptr<Task> g_task;

void Complete(int i) {
  g_task->complele(i);
  g_task.reset();
}

void AsyncIO() {
  g_task->complele = [](int i) { g_task->step3(i + 1); };
}

void Step3() {
  g_task->step3 = [](int i) { g_task->step2(i + 1); };
  AsyncIO();
}

void Step2() {
  g_task->step2 = [](int i) { g_task->step1(i + 1); };
  Step3();
}

void Step1() {
  g_task->step1 = [](int i) { g_task->cb(i + 1); };
  Step2();
}

void Run(std::function<void(int)>&& cb) {
  g_task = std::make_unique<Task>();
  g_task->cb = [cb = std::move(cb)](int i) { cb(i + 1); };
  Step1();
}

}  // namespace async_task

namespace mfuture_async {

mfuture::Future<int> AsyncIO(mfuture::Promise<int>& promise) {
  return promise.GetFuture().Then([](int i) { return i + 1; });
}

mfuture::Future<int> Step3(mfuture::Promise<int>& promise) {
  return AsyncIO(promise).Then([](int i) { return i + 1; });
}

mfuture::Future<int> Step2(mfuture::Promise<int>& promise) {
  return Step3(promise).Then([](int i) { return i + 1; });
}

mfuture::Future<int> Step1(mfuture::Promise<int>& promise) {
  return Step2(promise).Then([](int i) { return i + 1; });
}

mfuture::Future<int> Run(mfuture::Promise<int>& promise) {
  return Step1(promise).Then([](int i) { return i + 1; });
}

}  // namespace mfuture_async

namespace nfuture_async {

nfuture::Future<int> AsyncIO(nfuture::Promise<int>& promise) {
  return promise.GetFuture().Then([](int i) { return i + 1; });
}

nfuture::Future<int> Step3(nfuture::Promise<int>& promise) {
  return AsyncIO(promise).Then([](int i) { return i + 1; });
}

nfuture::Future<int> Step2(nfuture::Promise<int>& promise) {
  return Step3(promise).Then([](int i) { return i + 1; });
}

nfuture::Future<int> Step1(nfuture::Promise<int>& promise) {
  return Step2(promise).Then([](int i) { return i + 1; });
}

nfuture::Future<int> Run(nfuture::Promise<int>& promise) {
  // This can't be optimized out.
  return Step1(promise).Then([](int i) { return i + 1; });
}

}  // namespace nfuture_async

namespace nfuture_coro {

nfuture::Future<int> AsyncIO(nfuture::Promise<int>& promise) {
  auto i = co_await promise.GetFuture();
  co_return i + 1;
}

nfuture::Future<int> Step3(nfuture::Promise<int>& promise) {
  auto i = co_await AsyncIO(promise);
  co_return i + 1;
}

nfuture::Future<int> Step2(nfuture::Promise<int>& promise) {
  auto i = co_await Step3(promise);
  co_return i + 1;
}

nfuture::Future<int> Step1(nfuture::Promise<int>& promise) {
  auto i = co_await Step2(promise);
  co_return i + 1;
}

nfuture::Future<int> Run(nfuture::Promise<int>& promise) {
  auto i = co_await Step1(promise);
  co_return i + 1;
}

}  // namespace nfuture_coro

constexpr std::size_t kTimes = 100000;

TEST(async, lambda) {
  for (std::size_t i = 0; i < kTimes; ++i) {
    async_lambda::Run([](int i) { EXPECT_EQ(i, 6); });
    async_lambda::Complete(1);
  }
}

TEST(async, task) {
  for (std::size_t i = 0; i < kTimes; ++i) {
    async_task::Run([](int i) { EXPECT_EQ(i, 6); });
    async_task::Complete(1);
  }
}

TEST(mfuture, async) {
  int counter = 0;
  auto future = mfuture::DoUntil([n = kTimes]() mutable { return n-- == 0; },
                                 [&counter]() {
                                   ++counter;
                                   mfuture::Promise<int> promise;
                                   auto future = mfuture_async::Run(promise);
                                   EXPECT_FALSE(future.IsResolved());
                                   promise.SetValue(1);
                                   EXPECT_TRUE(future.IsReady());
                                   EXPECT_EQ(future.GetValue<0>(), 6);
                                   return mfuture::MakeReadyFuture<>();
                                 });
  EXPECT_TRUE(future.IsReady());
  ASSERT_EQ(counter, kTimes);
}

TEST(nfuture, async) {
  int counter = 0;
  auto future = nfuture::DoUntil([n = kTimes]() mutable { return n-- == 0; },
                                 [&counter]() {
                                   ++counter;
                                   nfuture::Promise<int> promise;
                                   auto future = nfuture_async::Run(promise);
                                   EXPECT_FALSE(future.Available());
                                   promise.SetValue(1);
                                   EXPECT_TRUE(future.Ready());
                                   EXPECT_EQ(future.Value<0>(), 6);
                                   return nfuture::MakeReadyFuture<>();
                                 });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);

  std::cout << "Scheduled " << nfuture::Promise<int>::Scheduled() << std::endl;
  auto count = nfuture::details::ContinuationBase<int>::Count();
  std::cout << "Continuation newed " << count.first << " deleted " << count.second << std::endl;
  count = nfuture::details::CPromise<int>::Count();
  std::cout << "Frame newed " << count.first << " deleted " << count.second << std::endl;
}

TEST(nfuture, coro) {
  int counter = 0;
  auto future = nfuture::DoUntil([n = kTimes]() mutable { return n-- == 0; },
                                 [&counter]() {
                                   ++counter;
                                   nfuture::Promise<int> promise;
                                   auto future = nfuture_coro::Run(promise);
                                   EXPECT_FALSE(future.Available());
                                   promise.SetValue(1);
                                   EXPECT_TRUE(future.Ready());
                                   EXPECT_EQ(future.Value<0>(), 6);
                                   return nfuture::MakeReadyFuture<>();
                                 });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);

  std::cout << "Scheduled " << nfuture::Promise<int>::Scheduled() << std::endl;
  auto count = nfuture::details::ContinuationBase<int>::Count();
  std::cout << "Continuation newed " << count.first << " deleted " << count.second << std::endl;
  count = nfuture::details::CPromise<int>::Count();
  std::cout << "Frame newed " << count.first << " deleted " << count.second << std::endl;
}