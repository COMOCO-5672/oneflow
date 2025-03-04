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
#include "oneflow/core/kernel/kernel.h"
#include "oneflow/core/common/buffer_manager.h"
#include "oneflow/core/job/critical_section_instance.h"
#include "oneflow/core/job/global_for.h"

namespace oneflow {

class ReturnKernel final : public Kernel {
 public:
  OF_DISALLOW_COPY_AND_MOVE(ReturnKernel);
  ReturnKernel() = default;
  ~ReturnKernel() = default;

 private:
  void ForwardDataContent(KernelContext* ctx) const override;
  void ForwardHeader(KernelContext* ctx) const override;
};

void ReturnKernel::ForwardDataContent(KernelContext* ctx) const {
  CHECK(this->op_conf().return_conf().has_job_name());
  const auto& job_name = this->op_conf().return_conf().job_name();
  const auto& op_name = this->op_conf().name();
  auto* buffer_mgr = Singleton<BufferMgr<std::shared_ptr<CriticalSectionInstance>>>::Get();
  auto* buffer = buffer_mgr->Get(GetOutputBufferName(job_name, op_name));
  std::shared_ptr<CriticalSectionInstance> critical_section_instance;
  BufferStatus buffer_status = buffer->TryReceive(&critical_section_instance);
  CHECK_NE(buffer_status, kBufferStatusEmpty);
  if (buffer_status == kBufferStatusSuccess) {
    OfBlob ofblob(ctx->stream(), ctx->BnInOp2Blob("in"));
    critical_section_instance->AccessBlobByOpName(reinterpret_cast<uint64_t>(&ofblob), op_name);
  }
}

void ReturnKernel::ForwardHeader(KernelContext* ctx) const {
  // Do nothing.
}

REGISTER_KERNEL(OperatorConf::kReturnConf, ReturnKernel);

}  // namespace oneflow
