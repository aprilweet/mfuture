
// Copyright (c) 2024, Tencent Inc.
// All rights reserved.

#pragma once

#include "nfuture.h"

namespace nfuture {

namespace details {

template <typename FutureType>
struct ContinuationBaseOfFuture;

template <typename... T>
struct ContinuationBaseOfFuture<Future<T...>> {
  using type = ContinuationBase<T...>;
};

template <typename FutureType>
using ContinuationBaseOfFuture_t = typename ContinuationBaseOfFuture<FutureType>::type;

template <typename FutureType, typename... Objects>
struct DoWithState : public ContinuationBaseOfFuture_t<FutureType> {
  DoWithState(Objects&&... objects) : objects_(std::forward<Objects>(objects)...) {}

  void Run() override {
    assert(this->state_.Available());
    if (this->state_.Failed()) {
      promise_.SetException(std::move(this->state_).Exception());
    } else {
      promise_.SetValue(details::SetValueTag{}, std::move(this->state_).Value());
    }
    delete this;
  }

  std::tuple<Objects...> objects_;
  typename FutureType::PromiseType promise_;
};

}  // namespace details

template <typename AsyncFunc, typename Object, typename... MoreObjects>
auto DoWith(AsyncFunc&& f, Object&& obj, MoreObjects&&... more) {
  using R = std::invoke_result_t<AsyncFunc, Object&, MoreObjects&...>;
  static_assert(details::IsFuture_v<R>);
  // FIXME(quintonwang): Object reference type?
  auto state = new details::DoWithState<R, Object, MoreObjects...>(std::forward<Object>(obj),
                                                                   std::forward<MoreObjects>(more)...);
  auto future = std::apply(std::forward<AsyncFunc>(f), state->objects_);
  if (future.Available()) {
    delete state;
    return future;
  }
  details::SetContinuation(future, state);
  return state->promise_.GetFuture();
}

}