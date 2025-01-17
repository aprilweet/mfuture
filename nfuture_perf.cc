#include "nfuture.h"

#include <iostream>
#include <functional>

#include "gtest/gtest.h"

namespace async {

nfuture::Future<int> AsyncIO(nfuture::Promise<int>& promise) {
  return promise.GetFuture();
}

nfuture::Future<int> Step3(nfuture::Promise<int>& promise) {
  return AsyncIO(promise);
}

nfuture::Future<int> Step2(nfuture::Promise<int>& promise) {
  return Step3(promise);
}

nfuture::Future<int> Step1(nfuture::Promise<int>& promise) {
  return Step2(promise);
}

nfuture::Future<int> Run(nfuture::Promise<int>& promise) {
  // This can't be optimized out.
  return Step1(promise).Then([](int i) { return i + 1; });
}

}  // namespace async

namespace coro {

nfuture::Future<int> AsyncIO(nfuture::Promise<int>& promise) {
  co_return co_await promise.GetFuture();
}

nfuture::Future<int> Step3(nfuture::Promise<int>& promise) {
  co_return co_await AsyncIO(promise);
}

nfuture::Future<int> Step2(nfuture::Promise<int>& promise) {
  co_return co_await Step3(promise);
}

nfuture::Future<int> Step1(nfuture::Promise<int>& promise) {
  co_return co_await Step2(promise);
}

nfuture::Future<int> Run(nfuture::Promise<int>& promise) {
  auto i = co_await Step1(promise);
  co_return i + 1;
}

}  // namespace coro

constexpr std::size_t kTimes = 100000;

TEST(nfuture, async) {
  int counter = 0;
  auto future = nfuture::DoUntil([n = kTimes]() mutable { return n-- == 0; },
                                 [&counter]() {
                                   ++counter;
                                   nfuture::Promise<int> promise;
                                   auto future = async::Run(promise);
                                   EXPECT_FALSE(future.Available());
                                   promise.SetValue(1);
                                   EXPECT_TRUE(future.Ready());
                                   EXPECT_EQ(future.Value<0>(), 2);
                                   return nfuture::MakeReadyFuture<>();
                                 });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);

  std::cout << "Scheduled " << nfuture::Promise<int>::Scheduled() << std::endl;
  auto count = nfuture::details::ContinuationBase<int>::Count();
  std::cout << "Continuation newed " << count.first << " deleted "
            << count.second << std::endl;
  count = nfuture::details::CPromise<int>::Count();
  std::cout << "Frame newed " << count.first << " deleted " << count.second
            << std::endl;
}

TEST(nfuture, coro) {
  int counter = 0;
  auto future = nfuture::DoUntil([n = kTimes]() mutable { return n-- == 0; },
                                 [&counter]() {
                                   ++counter;
                                   nfuture::Promise<int> promise;
                                   auto future = coro::Run(promise);
                                   EXPECT_FALSE(future.Available());
                                   promise.SetValue(1);
                                   EXPECT_TRUE(future.Ready());
                                   EXPECT_EQ(future.Value<0>(), 2);
                                   return nfuture::MakeReadyFuture<>();
                                 });
  EXPECT_TRUE(future.Ready());
  ASSERT_EQ(counter, kTimes);

  std::cout << "Scheduled " << nfuture::Promise<int>::Scheduled() << std::endl;
  auto count = nfuture::details::ContinuationBase<int>::Count();
  std::cout << "Continuation newed " << count.first << " deleted "
            << count.second << std::endl;
  count = nfuture::details::CPromise<int>::Count();
  std::cout << "Frame newed " << count.first << " deleted " << count.second
            << std::endl;
}