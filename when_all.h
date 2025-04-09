// Copyright (c) 2024, Tencent Inc.
// All rights reserved.

#pragma once

#include <iterator>
#include <memory>
#include <vector>

#include "nfuture.h"

namespace fabric {

namespace details {

template <typename FutureType>
struct WhenAllState {
  std::vector<FutureType> futures;
  std::size_t counter{0};
  Promise<std::vector<FutureType>> promise;
};

}  // namespace details

template <typename FutureIterator, typename FutureType = typename std::iterator_traits<FutureIterator>::value_type>
Future<std::vector<FutureType>> WhenAll(FutureIterator begin, FutureIterator end) {
  auto count = static_cast<std::size_t>(std::abs(std::distance(begin, end)));
  if (count == 0) {
    return MakeReadyFuture<std::vector<FutureType>>(std::vector<FutureType>());
  } else if (count == 1) {
    return (*begin).ThenWrap([](FutureType &&future) {
      // FIXME(quintonwang): Compile error.
      // return std::vector<FutureType>({std::move(future)});
      std::vector<FutureType> ret;
      ret.push_back(std::move(future));
      return ret;
    });
  } else {
    auto state = std::make_shared<details::WhenAllState<FutureType>>();
    state->futures.resize(count);
    std::size_t index = 0;
    for (auto iter = begin; iter != end; ++iter) {
      (*iter).ThenWrap([state, count, idx = index++](FutureType &&future) {
        state->futures[idx] = std::move(future);
        assert(state->counter < count);
        if (++state->counter == count) state->promise.SetValue(std::move(state->futures));
      }).Ignore();
    }
    assert(index == count);
    return state->promise.GetFuture();
  }
}

}  // namespace fabric