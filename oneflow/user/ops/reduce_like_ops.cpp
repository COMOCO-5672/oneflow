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
#include "oneflow/core/operator/reduce_sbp_util.h"
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

/*static*/ Maybe<void> ReduceSumLikeOp::GetSbp(user_op::SbpContext* ctx) {
  int32_t num_axes = 0;
  HashSet<int32_t> conf_axes;
  {
    const auto& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("x", 0);
    num_axes = in_tensor.shape().NumAxes();
    const auto& reduced_axes = ctx->Attr<std::vector<int32_t>>("axis");
    ReduceSbpUtil::GetRegularAxes(num_axes, reduced_axes, &conf_axes);
  }

  const auto& like_num_axes =
      ctx->LogicalTensorDesc4InputArgNameAndIndex("like", 0).shape().NumAxes();
  const bool keep_dims = (num_axes == like_num_axes);
  if (!keep_dims) {
    CHECK_EQ_OR_RETURN(conf_axes.size(), num_axes - like_num_axes)
        << Error::RuntimeError()
        << "The size of axis list must be equal to the difference of the dimension "
        << "between x tensor and like tensor";
  }
  auto IsReducedAxis = ReduceSbpUtil::MakePredicatorIsReducedAxis(conf_axes, num_axes);
  int64_t num_reduced_axes = 0;
  FOR_RANGE(int64_t, i, 0, num_axes) {
    if (IsReducedAxis(i)) {
      ctx->NewBuilder()
          .Split(user_op::OpArg("x", 0), i)
          .Broadcast(user_op::OpArg("like", 0))
          .PartialSum(user_op::OpArg("y", 0))
          .Build();
      ctx->NewBuilder()
          .Split(user_op::OpArg("x", 0), i)
          .PartialSum(user_op::OpArg("like", 0))
          .PartialSum(user_op::OpArg("y", 0))
          .Build();
      num_reduced_axes += 1;
    } else {
      const int64_t out_split_axis = keep_dims ? i : i - num_reduced_axes;
      ctx->NewBuilder()
          .Split(user_op::OpArg("x", 0), i)
          .Split(user_op::OpArg("like", 0), out_split_axis)
          .Split(user_op::OpArg("y", 0), out_split_axis)
          .Build();
    }
  }
  ctx->NewBuilder()
      .Broadcast(user_op::OpArg("x", 0))
      .PartialSum(user_op::OpArg("like", 0))
      .Broadcast(user_op::OpArg("y", 0))
      .Build();
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> ReduceSumLikeOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const user_op::TensorDesc& x_tensor = ctx->InputTensorDesc("x", 0);
  const user_op::TensorDesc& like_tensor = ctx->InputTensorDesc("like", 0);
  const auto& axis = ctx->Attr<std::vector<int32_t>>("axis");
  if (axis.empty()) {
    CHECK_EQ_OR_RETURN(x_tensor.shape(), like_tensor.shape())
        << Error::RuntimeError()
        << "The shape of the x tensor must be consistent to the shape of the like tensor"
        << " when the input axis list is empty";
  }

  user_op::TensorDesc* y_tensor = ctx->OutputTensorDesc("y", 0);
  *y_tensor->mut_shape() = like_tensor.shape();
  *y_tensor->mut_is_dynamic() = like_tensor.is_dynamic();
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> ReduceSumLikeOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}
/*static*/ Maybe<void> ReduceSumLikeOp::InferDataType(user_op::InferContext* ctx) {
  const user_op::TensorDesc& x_tensor = ctx->InputTensorDesc("x", 0);
  const user_op::TensorDesc& like_tensor = ctx->InputTensorDesc("like", 0);
  CHECK_EQ_OR_RETURN(x_tensor.data_type(), like_tensor.data_type())
      << Error::TypeError() << "Tensors x and like must have the same type";
  *ctx->OutputDType("y", 0) = like_tensor.data_type();
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> ReduceSumLikeOp::ModifyInputArg(
    const GetInputArgModifier& GetInputArgModifierFn, const user_op::UserOpConfWrapper&) {
  user_op::InputArgModifier* like_arg_modifier = GetInputArgModifierFn("like", 0);
  CHECK_OR_RETURN(like_arg_modifier != nullptr);  // NOLINT(maybe-need-error-msg)
  like_arg_modifier->set_requires_grad(false);
  return Maybe<void>::Ok();
}

}  // namespace oneflow
