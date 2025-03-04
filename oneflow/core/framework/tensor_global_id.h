/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef ONEFLOW_CORE_FRAMEWORK_TENSOR_CONSISTENT_ID_
#define ONEFLOW_CORE_FRAMEWORK_TENSOR_CONSISTENT_ID_

#include "oneflow/core/common/maybe.h"

namespace oneflow {

Maybe<TransportToken> NewTensorGlobalId();

namespace one {

class TensorTuple;

int64_t* MutThreadLocalGlobalIdDepth();
Maybe<void> InitGlobalId(TensorTuple* outputs);

template<typename... Args>
struct NonRecursiveInitGlobalId;

template<typename Arg0, typename Arg1, typename... Args>
struct NonRecursiveInitGlobalId<Maybe<void>, Arg0, Arg1, TensorTuple*, Args...> {
  template<Maybe<void> (*func)(Arg0, Arg1, TensorTuple*, Args...)>
  static Maybe<void> Call(Arg0 arg0, Arg1 arg1, TensorTuple* outputs, Args... args) {
    auto* recursive_depth = MutThreadLocalGlobalIdDepth();
    ++*recursive_depth;
    Maybe<void> ret = func(arg0, arg1, outputs, args...);
    --*recursive_depth;
    if (*recursive_depth == 0 && ret.IsOk()) { JUST(InitGlobalId(outputs)); }
    return ret;
  }
};

}  // namespace one

}  // namespace oneflow

#endif  // ONEFLOW_CORE_FRAMEWORK_TENSOR_CONSISTENT_ID_
