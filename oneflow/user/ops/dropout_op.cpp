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

/* static */ Maybe<void> DropoutOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const Shape& in_shape = ctx->InputShape("in", 0);
  *ctx->OutputShape("out", 0) = in_shape;
  *ctx->OutputShape("mask", 0) = in_shape;
  *ctx->OutputIsDynamic("out", 0) = ctx->InputIsDynamic("in", 0);
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> DropoutOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> DropoutOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
  FOR_RANGE(int64_t, axis, 0, in_tensor.shape().NumAxes()) {
    ctx->NewBuilder().Split(ctx->inputs(), axis).Split(ctx->outputs(), axis).Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> DropoutOp::CheckAttr(const user_op::UserOpDefWrapper& def,
                                              const user_op::UserOpConfWrapper& conf) {
  float rate = conf.attr<float>("rate");
  CHECK_GE_OR_RETURN(rate, 0.0);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> DropoutOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->OutputDType("out", 0) = ctx->InputDType("in", 0);
  *ctx->OutputDType("mask", 0) = DataType::kBool;
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> DropoutGradOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const Shape& dy_shape = ctx->InputShape("dy", 0);
  *ctx->OutputShape("dx", 0) = dy_shape;
  *ctx->OutputIsDynamic("dx", 0) = ctx->InputIsDynamic("dy", 0);
  CHECK_EQ_OR_RETURN(ctx->InputShape("mask", 0), dy_shape);
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> DropoutGradOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> DropoutGradOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& dy_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("dy", 0);
  FOR_RANGE(int64_t, axis, 0, dy_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("dy", 0), axis)
        .Split(user_op::OpArg("mask", 0), axis)
        .Split(user_op::OpArg("dx", 0), axis)
        .Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> DropoutGradOp::CheckAttr(const user_op::UserOpDefWrapper& def,
                                                  const user_op::UserOpConfWrapper& conf) {
  float scale = conf.attr<float>("scale");
  CHECK_GT_OR_RETURN(scale, 1);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> DropoutGradOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->OutputDType("dx", 0) = ctx->InputDType("dy", 0);
  CHECK_EQ_OR_RETURN(ctx->InputDType("mask", 0), DataType::kBool);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> RandomMaskLikeOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  *ctx->OutputShape("out", 0) = ctx->InputShape("like", 0);
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> RandomMaskLikeOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> RandomMaskLikeOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& like_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("like", 0);
  FOR_RANGE(int64_t, axis, 0, like_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("like", 0), axis)
        .Split(user_op::OpArg("out", 0), axis)
        .Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> RandomMaskLikeOp::CheckAttr(const user_op::UserOpDefWrapper& def,
                                                     const user_op::UserOpConfWrapper& conf) {
  float rate = conf.attr<float>("rate");
  CHECK_GE_OR_RETURN(rate, 0);
  CHECK_LT_OR_RETURN(rate, 1);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> RandomMaskLikeOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->OutputDType("out", 0) = DataType::kBool;
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("dropout").SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                                                           user_op::AddOpFn AddOp) -> Maybe<void> {
  if (op.NeedGenGradTensor4OpInput("in", 0)) {
    user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
    const float rate = op.attr<float>("rate");
    float scale = 0.0f;  // When dropout rate = 1.0, we set scale as zero.
    if (rate < 1.0f) { scale = 1.0f / (1.0f - rate); }
    user_op::UserOpConfWrapper dropout_grad_op =
        builder.Op("dropout_grad")
            .Input("dy", op.GetGradTensorWithOpOutput("out", 0))
            .Input("mask", op.output("mask", 0))
            .Output("dx")
            .Attr("scale", scale)
            .Build();
    op.BindGradTensorWithOpInput(dropout_grad_op.output("dx", 0), "in", 0);
    AddOp(dropout_grad_op);
  }
  return Maybe<void>::Ok();
});

}  // namespace oneflow
