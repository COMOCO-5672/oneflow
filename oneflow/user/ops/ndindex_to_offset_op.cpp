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
#include "oneflow/core/framework/framework.h"
namespace oneflow {

namespace user_op {

REGISTER_USER_OP("ndindex_to_offset")
    .Input("index")
    .Input("dims")
    .Output("out")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const TensorDesc* index = ctx->TensorDesc4ArgNameAndIndex("index", 0);
      const TensorDesc* dims = ctx->TensorDesc4ArgNameAndIndex("dims", 0);
      int64_t index_num_axes = index->shape().NumAxes();
      CHECK_GT_OR_RETURN(index_num_axes, 0);
      int64_t dims_num_axes = dims->shape().NumAxes();
      CHECK_GT_OR_RETURN(dims_num_axes, 0);
      CHECK_EQ(index->shape().elem_cnt(), dims->shape().elem_cnt());
      user_op::TensorDesc* out = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      *out->mut_shape() = Shape({1});  // Only return single value
      *out->mut_data_type() = dims->data_type();
      return Maybe<void>::Ok();
    })
    .SetBatchAxisInferFn([](user_op::BatchAxisContext* ctx) -> Maybe<void> {
      return user_op::BatchAxisInferFnUtil::NaiveInferBatchAxis(ctx);
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Broadcast(ctx->inputs()).Broadcast(ctx->outputs()).Build();
      return Maybe<void>::Ok();
    });

}  // namespace user_op

}  // namespace oneflow