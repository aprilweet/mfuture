
// Copyright (c) 2024, Tencent Inc.
// All rights reserved.

#pragma once

#include "nfuture.h"

namespace fabric {

namespace details {

template <typename Stop, typename Func>
struct DoUntilState : public ContinuationBase<> {
  DoUntilState(Future<>&& future, Stop&& stop, Func&& func)
      : future_(std::move(future)), stop_(std::forward<Stop>(stop)), function_(std::forward<Func>(func)) {
    details::SetContinuation(future_, this);
  }

  void Run() override {
    assert(state_.Available());
    if (state_.Failed()) {
      promise_.SetException(std::move(state_).Exception());
      delete this;
      return;
    }
    do {
      if (stop_()) {
        promise_.SetValue();
        state_.Reset();
        delete this;
        break;
      } else {
        future_ = FuturizeInvoke(function_);
        if (future_.Ready()) {
        } else if (future_.Failed()) {
          promise_.SetException(future_.Exception());
          state_.Reset();
          delete this;
          break;
        } else {
          SetContinuation(future_, this);
          break;
        }
      }
    } while (true);
  }

  Future<> future_;  // Keep it alive in case that it's a coroutine.
  Promise<> promise_;
  Stop stop_;
  Func function_;
};

}  // namespace details

template <typename Stop, typename Func>
Future<> DoUntil(Stop &&stop, Func &&func) {
  static_assert(std::is_convertible_v<std::invoke_result_t<Stop>, bool>);

  // If Func doesn't return a Future, user should use do-while instead.
  using R = std::invoke_result_t<Func>;
  static_assert(std::is_same_v<R, Future<>>);

  do {  // Fast path.
    if (stop()) return MakeReadyFuture<>();
    auto future = FuturizeInvoke(func);
    if (future.Ready())
      // Never use Then() to drive here to avoid stack overflow.
      continue;
    else if (future.Failed())
      return future;
    else {
      auto state =
          new details::DoUntilState<Stop, Func>(std::move(future), std::forward<Stop>(stop), std::forward<Func>(func));
      return state->promise_.GetFuture();
    }
  } while (true);
}

template <typename Func>
Future<> Repeat(Func &&func) {
  return DoUntil([]() { return false; }, std::forward<Func>(func));
}

template <typename Func>
Future<> Repeat(std::size_t times, Func &&func) {
  return DoUntil([times]() mutable { return times-- == 0; }, std::forward<Func>(func));
}

template <typename Iterator, typename Func>
Future<> DoForEach(Iterator &&begin, Iterator &&end, Func &&func) {
  // TODO(quintonwang): &&.
  if constexpr (std::is_invocable_v<Func, typename Iterator::value_type &>) {
    // TODO(quintonwang): Opt.
    auto state = std::make_shared<Iterator>(std::forward<Iterator>(begin));
    return DoUntil([state, end = std::forward<Iterator>(end)]() { return (*state) == end; },
                   [state, func = std::forward<Func>(func)]() { return func(*(*state)++); });
  } else {
    static_assert(std::is_invocable_v<Func>);
    return DoUntil(
        [begin = std::forward<Iterator>(begin), end = std::forward<Iterator>(end)]() mutable { return begin++ == end; },
        std::forward<Func>(func));
  }
}

}  // namespace fabric