// Copyright (c) 2024, Tencent Inc.
// All rights reserved.

#pragma once

#include <deque>
#include <memory>
#include <queue>

#include "nfuture.h"

namespace nfuture {

class Semaphore {
 public:
  enum class Policy { kFifo = 0, kFast };

  Semaphore(std::size_t initial, Policy policy = Policy::kFifo)
      : Semaphore(initial, std::numeric_limits<std::size_t>::max(), policy) {}

  Semaphore(std::size_t initial, std::size_t limit, Policy policy = Policy::kFifo)
      : policy_(policy), limit_(limit), available_(initial) {
    if (policy_ == Policy::kFifo) {
      waiters_ = std::make_unique<FifoPolicyStore>(available_);
    } else {
      assert(policy == Policy::kFast);
      waiters_ = std::make_unique<FastPolicyStore>(available_);
    }
  }

  Future<> Wait(std::size_t nr = 1) {
    assert(nr <= limit_);
    return waiters_->Wait(nr);
  }

  void Post(std::size_t nr) {
    available_ += nr;
    assert(available_ <= limit_);

    if (!waiters_->Size()) return;
    return waiters_->Post(nr);
  }

  std::size_t Available() const { return available_; }

 private:
  const Policy policy_;
  const std::size_t limit_;
  std::size_t available_;

  struct PolicyStore {
    virtual ~PolicyStore() = default;
    virtual std::size_t Size() const = 0;
    virtual Future<> Wait(std::size_t nr) = 0;
    virtual void Post(std::size_t nr) = 0;
  };
  std::unique_ptr<PolicyStore> waiters_;

  struct Waiter {
    Waiter(std::size_t nr) : nr_(nr) {}
    Waiter(std::size_t nr, Promise<>&& promise) : promise_(std::move(promise)), nr_(nr) {}

    mutable Promise<> promise_;
    std::size_t nr_;

    bool operator>(const Waiter& other) const { return nr_ > other.nr_; }
  };

  struct FifoPolicyStore : public PolicyStore {
    std::size_t& available_;
    std::deque<Waiter> waiters_;

   public:
    FifoPolicyStore(std::size_t& available) : available_(available) {}

    std::size_t Size() const override { return waiters_.size(); }

    Future<> Wait(std::size_t nr) override {
      if (available_ >= nr && waiters_.empty()) {
        available_ -= nr;
        return MakeReadyFuture<>();
      }
      auto& waiter = waiters_.emplace_back(nr);
      return waiter.promise_.GetFuture();
    }

    void Post(std::size_t nr) override {
      while (!waiters_.empty()) {
        auto& waiter = waiters_.front();
        if (available_ < waiter.nr_) break;

        available_ -= waiter.nr_;
        waiter.promise_.SetValue();
        waiters_.pop_front();
      }
    }
  };

  class FastPolicyStore : public PolicyStore {
    std::size_t& available_;
    std::priority_queue<Waiter, std::deque<Waiter>, std::greater<Waiter>> waiters_;

   public:
    FastPolicyStore(std::size_t& available) : available_(available) {}

    std::size_t Size() const override { return waiters_.size(); }

    Future<> Wait(std::size_t nr) override {
      if (available_ >= nr) {
        if (!waiters_.empty()) {
          assert(nr < waiters_.top().nr_);
        }
        available_ -= nr;
        return MakeReadyFuture<>();
      }
      Future<> future;
      waiters_.emplace(nr, future.GetPromise());
      return future;
    }

    void Post(std::size_t nr) override {
      while (!waiters_.empty()) {
        auto& waiter = waiters_.top();
        if (available_ < waiter.nr_) break;

        available_ -= waiter.nr_;
        waiter.promise_.SetValue();
        waiters_.pop();
      }
    }
  };
};

}  // namespace nfuture
