// Copyright (c) 2024, Tencent Inc.
// All rights reserved.

#pragma once

#include "semaphore.h"

namespace nfuture {

class Latch {
  const std::size_t limit_;
  std::size_t count_;  // For debug
  Semaphore semaphore_;

 public:
  Latch(std::size_t count) : limit_(count), count_(count), semaphore_(0, count) {}

  void CountDown(std::size_t value = 1) {
    semaphore_.Post(value);
    count_ -= value;
    assert(count_ == semaphore_.Available());
  }

  Future<> Wait() { return semaphore_.Wait(limit_); }
};

}  // namespace nfuture
