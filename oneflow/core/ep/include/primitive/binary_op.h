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
#ifndef ONEFLOW_CORE_EP_PRIMITIVE_BINARY_OP_H_
#define ONEFLOW_CORE_EP_PRIMITIVE_BINARY_OP_H_

#include "oneflow/core/ep/include/primitive/primitive.h"

namespace oneflow {

namespace ep {
namespace primitive {

enum class BinaryOp {
  // Math
  kAdd,
  kSub,
  kMul,
  kDiv,
  kMax,
  kMin,
  kPow,
  // Comparision
  kEqual,
  kNotEqual,
  kLessThan,
  kLessEqual,
  kGreaterThan,
  kGreaterEqual,
  // Logical
  kLogicalAnd,
  kLogicalOr,
  kLogicalXor,
  // Unary Backward
  kEluBackwardWithDyX,
  kCeluBackwardWithDyX,
  kGeluBackwardWithDyX,
  kHardswishBackwardWithDyX,
  kHardsigmoidBackwardWithDyX,
  kHardshrinkBackwardWithDyY,
  kHardtanhBackwardWithDyY,
  kLeakyReluBackwardWithDyX,
  kMishBackwardWithDyX,
  kReluBackwardWithDyY,
  kSeluBackwardWithDyX,
  kSiluBackwardWithDyX,
  kSoftsignBackwardWithDyX,
  kSoftplusBackwardWithDyX,
  kSoftshrinkBackwardWithDyY,
  kTanhBackwardWithDyX,
  kThresholdBackwardWithDyX,
  kSigmoidBackwardWithDyY,

};

}
}  // namespace ep

}  // namespace oneflow

#endif  // ONEFLOW_CORE_EP_PRIMITIVE_BINARY_OP_H_
