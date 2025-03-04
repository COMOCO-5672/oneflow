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
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/tensor_methods.h"
#include "oneflow/core/framework/tensor_name_scope.h"
#include "oneflow/core/framework/tensor_rpc_util.h"
#include "oneflow/core/framework/nd_sbp.h"
#include "oneflow/core/common/maybe.h"
#include "oneflow/core/job/parallel_desc.h"
#include "oneflow/core/job/job_build_and_infer_ctx_mgr.h"
#include "oneflow/core/job/job_build_and_infer_ctx.h"
#include "oneflow/core/framework/device.h"
#include "oneflow/core/framework/dtype.h"
#include "oneflow/core/framework/tensor_tuple.h"
#include "oneflow/core/autograd/autograd_engine.h"
#include "oneflow/core/framework/op_interpreter/eager_local_op_interpreter.h"
#include "oneflow/core/functional/functional.h"

namespace oneflow {

namespace one {

Maybe<void> Tensor::BorrowTensorName(const Tensor* other) const {
  CHECK_OR_RETURN(other->is_lazy())
      << Error::RuntimeError() << "can not borrow tensor name from an eager tensor";
  const auto& lbn = TensorNameScope::Global()->Lookup(other);
  CHECK_OR_RETURN(!lbn.empty()) << "the input lazy tensor has no tensor name";
  TensorNameScope::Global()->Record(this, lbn);
  return Maybe<void>::Ok();
}

Maybe<LocalTensor> StaticZerosTensor::AsLocalTensor() {
  CHECK_OR_RETURN(is_local());  // NOLINT
  return std::dynamic_pointer_cast<LocalTensor>(
      JUST(functional::Constant(*shape_, Scalar(0), CHECK_JUST(DType::Get(dtype_)), device_)));
}

std::shared_ptr<Tensor> Parameter::contiguous() const {
  const auto& tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  if (tensor_->is_contiguous()) { return tensor; }
  return CHECK_JUST(functional::ToContiguous(tensor));
}

std::shared_ptr<Tensor> Parameter::pin_memory() const {
  std::shared_ptr<Tensor> tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  return CHECK_JUST(functional::PinMemory(tensor));
}

/* static */ Maybe<LocalTensor> LocalTensor::MakeTensor(const std::shared_ptr<const Shape>& shape,
                                                        const std::shared_ptr<const Stride>& stride,
                                                        DataType dtype,
                                                        const Symbol<Device>& device, bool is_lazy,
                                                        bool requires_grad, bool is_leaf) {
  const auto& tensor_meta =
      std::make_shared<LocalTensorMeta>(std::make_shared<Shape>(*shape), dtype, device);
  if (is_lazy) {
    const auto& impl = std::make_shared<LazyLocalTensorImpl>(tensor_meta, requires_grad, is_leaf);
    return std::make_shared<LocalTensor>(impl);
  } else {
    const auto& impl = std::make_shared<EagerLocalTensorImpl>(tensor_meta, requires_grad, is_leaf);
    return std::make_shared<LocalTensor>(impl);
  }
}

bool LocalTensor::is_cuda() const { return CHECK_JUST(device())->type() == "cuda"; }

Maybe<Tensor> LocalTensor::detach() const {
  std::shared_ptr<Tensor> tensor = std::make_shared<LocalTensor>(JUST(impl_->detach()));
  if (this->is_lazy()) { JUST(tensor->BorrowTensorName(this)); }
  return tensor;
}

std::shared_ptr<Tensor> LocalTensor::contiguous() const {
  std::shared_ptr<Tensor> tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  if (tensor->is_contiguous()) { return tensor; }
  return CHECK_JUST(functional::ToContiguous(tensor));
}

std::shared_ptr<Tensor> LocalTensor::pin_memory() const {
  std::shared_ptr<Tensor> tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  return CHECK_JUST(functional::PinMemory(tensor));
}

Maybe<Tensor> LocalTensor::clone() const {
  const auto& device_type = JUST(this->device())->type();
  int64_t device_id = JUST(this->device())->device_id();
  std::shared_ptr<Tensor> input = std::const_pointer_cast<Tensor>(shared_from_this());
  const bool pin_memory = JUST(JUST(input->AsLocalTensor())->is_pinned());
  return JUST(functional::Copy(input, device_type, device_id, /*pin_memory=*/pin_memory));
}

Maybe<void> LocalTensor::set_data(const std::shared_ptr<Tensor>& other) {
  CHECK_OR_RETURN(this->is_leaf()) << "Can only set leaf tensor's data.";
  const auto& mirrored_tensor = std::dynamic_pointer_cast<LocalTensor>(JUST(other->detach()));
  CHECK_NOTNULL_OR_RETURN(mirrored_tensor)
      << "Can not set a global tensor to the data of a local tensor";
  bool old_requires_grad = requires_grad();
  impl_ = mirrored_tensor->impl_;
  JUST(set_requires_grad(old_requires_grad));
  grad_fn_node_ = nullptr;
  if (other->is_lazy()) { JUST(this->BorrowTensorName(other.get())); }
  return Maybe<void>::Ok();
}

std::shared_ptr<Tensor> GlobalTensor::contiguous() const {
  std::shared_ptr<Tensor> tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  if (tensor->is_contiguous()) { return tensor; }
  return CHECK_JUST(functional::ToContiguous(tensor));
}

std::shared_ptr<Tensor> GlobalTensor::pin_memory() const {
  std::shared_ptr<Tensor> tensor = std::const_pointer_cast<Tensor>(shared_from_this());
  return CHECK_JUST(functional::PinMemory(tensor));
}

Maybe<Tensor> GlobalTensor::clone() const {
  const auto& local_tensor = JUST(cur_rank_phy_tensor());
  const auto& device_type = JUST(local_tensor->device())->type();
  int64_t device_id = JUST(local_tensor->device())->device_id();
  const auto& cloned_local_tensor =
      JUST(functional::Copy(local_tensor, device_type, device_id, /*pin_memory=*/false));
  DisableCheckGlobalTensorMetaScope disable_meta_check{};
  return functional::LocalToGlobal(cloned_local_tensor, JUST(parallel_desc()),
                                   *JUST(GetSbpList(JUST(nd_sbp()))), *shape(), dtype());
}

Maybe<GlobalTensor> GlobalTensor::MakeTensor(const std::shared_ptr<const Shape>& shape,
                                             DataType dtype, Symbol<NdSbp> nd_sbp,
                                             Symbol<ParallelDesc> parallel_desc, bool is_lazy,
                                             bool requires_grad, bool is_leaf) {
  std::shared_ptr<GlobalTensorImpl> impl;
  Symbol<GlobalTensorMeta> global_tensor_meta(
      GlobalTensorMeta(shape, dtype, nd_sbp, parallel_desc));
  if (is_lazy) {
    impl = std::make_shared<LazyGlobalTensorImpl>(global_tensor_meta, requires_grad, is_leaf);
  } else {
    impl = JUST(EagerGlobalTensorImpl::New(global_tensor_meta, requires_grad, is_leaf));
  }
  return std::make_shared<GlobalTensor>(impl);
}

bool GlobalTensor::is_cuda() const {
  return CHECK_JUST(parallel_desc())->device_type() == DeviceType::kCUDA;
}

Maybe<Tensor> GlobalTensor::detach() const {
  std::shared_ptr<Tensor> tensor = std::make_shared<GlobalTensor>(JUST(impl_->detach()));
  if (this->is_lazy()) { JUST(tensor->BorrowTensorName(this)); }
  return tensor;
}

Maybe<void> GlobalTensor::set_data(const std::shared_ptr<Tensor>& other) {
  CHECK_OR_RETURN(this->is_leaf())
      << "Only leaf tensor's data can be set, because non-leaf tensor's data has been captured in "
         "the backward graph in autograd.";
  const auto& global_tensor = std::dynamic_pointer_cast<GlobalTensor>(JUST(other->detach()));
  CHECK_NOTNULL_OR_RETURN(global_tensor);  // NOLINT
  JUST(WithConsistencyChecked(global_tensor, [&]() -> Maybe<void> { return Maybe<void>::Ok(); }));

  bool old_requires_grad = requires_grad();
  impl_ = global_tensor->impl_;
  JUST(set_requires_grad(old_requires_grad));
  grad_fn_node_ = nullptr;
  if (other->is_lazy()) { JUST(this->BorrowTensorName(other.get())); }
  return Maybe<void>::Ok();
}

}  // namespace one

}  // namespace oneflow
