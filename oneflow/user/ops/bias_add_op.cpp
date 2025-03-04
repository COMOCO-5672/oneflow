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
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

/* static */ Maybe<void> BiasAddOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const auto& a_tensor_desc = ctx->InputTensorDesc("a", 0);
  const auto& b_tensor_desc = ctx->InputTensorDesc("b", 0);
  const auto bias_add_axis = ctx->Attr<int32_t>("axis");
  CHECK_EQ_OR_RETURN(b_tensor_desc.shape().NumAxes(), 1)
      << Error::RuntimeError() << "Bias tensor has to be a one-dimensional vector";
  CHECK_GE_OR_RETURN(bias_add_axis, 0)
      << Error::RuntimeError() << "The size of the axis must greater than or equal to 0, "
      << "but got " << bias_add_axis;
  CHECK_LT_OR_RETURN(bias_add_axis, a_tensor_desc.shape().NumAxes())
      << Error::IndexError() << "Dimension out of range (expected to be in range of [0"
      << ", " << a_tensor_desc.shape().NumAxes() - 1 << "],"
      << " but got " << bias_add_axis << ")";
  CHECK_EQ_OR_RETURN(a_tensor_desc.shape().At(bias_add_axis), b_tensor_desc.shape().At(0))
      << Error::RuntimeError() << "The size of tensor " << a_tensor_desc.shape().ToString()
      << " must match the size of tensor " << b_tensor_desc.shape().ToString() << " at dimension "
      << bias_add_axis;
  *ctx->OutputShape("out", 0) = ctx->InputShape("a", 0);
  *ctx->OutputIsDynamic("out", 0) = ctx->InputIsDynamic("a", 0);
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> BiasAddOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> BiasAddOp::GetSbp(user_op::SbpContext* ctx) {
  const auto axis = ctx->Attr<int32_t>("axis");
  for (int64_t i = 0; i < ctx->LogicalTensorDesc4InputArgNameAndIndex("a", 0).shape().NumAxes();
       ++i) {
    if (i == axis) { continue; }
    ctx->NewBuilder()
        .Split(user_op::OpArg("a", 0), i)
        .Broadcast(user_op::OpArg("b", 0))
        .Split(ctx->outputs(), i)
        .Build();
  }
  ctx->NewBuilder()
      .Split(user_op::OpArg("b", 0), 0)
      .Split(user_op::OpArg("a", 0), axis)
      .Split(ctx->outputs(), axis)
      .Build();
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> BiasAddOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->OutputDType("out", 0) = ctx->InputDType("a", 0);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("bias_add")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               user_op::AddOpFn AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("a", 0)) {
        op.BindGradTensorWithOpInput(op.GetGradTensorWithOpOutput("out", 0), "a", 0);
      }
      if (op.NeedGenGradTensor4OpInput("b", 0)) {
        const int64_t num_axes = op.TensorDesc4ArgNameAndIndex("a", 0).shape().NumAxes();
        const int32_t bias_add_axis = op.attr<int32_t>("axis");
        std::vector<int32_t> reduce_axes_vec;
        FOR_RANGE(int64_t, i, 0, num_axes) {
          if (i != bias_add_axis) { reduce_axes_vec.emplace_back(i); }
        }
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        auto grad_op = builder.Op("reduce_sum")
                           .Input("input_tensor", op.GetGradTensorWithOpOutput("out", 0))
                           .Output("output_tensor")
                           .Attr("axis", reduce_axes_vec)
                           .Attr("keepdims", false)
                           .Build();
        AddOp(grad_op);
        op.BindGradTensorWithOpInput(grad_op.output("output_tensor", 0), "b", 0);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
