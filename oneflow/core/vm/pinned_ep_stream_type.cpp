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

#include "oneflow/core/vm/pinned_ep_stream_type.h"
#include "oneflow/core/common/maybe.h"
#include "oneflow/core/common/stream_role.h"
#include "oneflow/core/vm/instruction_type.h"
#include "oneflow/core/vm/stream.h"
#include "oneflow/core/vm/naive_stream_policy.h"
#include "oneflow/core/vm/thread_ctx.h"
#include "oneflow/core/vm/ep_optional_event_record_status_querier.h"
#include "oneflow/core/vm/ep_device_context.h"
#include "oneflow/core/vm/bin_allocator.h"
#include "oneflow/core/vm/ep_backend_host_allocator.h"
#include "oneflow/core/vm/thread_safe_guard.h"
#include "oneflow/core/common/util.h"
#include "oneflow/core/profiler/profiler.h"
#include "oneflow/core/ep/include/device_manager_registry.h"

namespace oneflow {
namespace vm {

void PinnedEpStreamType::InitDeviceCtx(std::unique_ptr<DeviceCtx>* device_ctx,
                                       Symbol<Device> device) const {
  // TODO:(zhaoluyang) empty/cast/copy op support pin_memory_device
  DeviceType device_type = device->enum_type();
  size_t device_index = device->device_id();
  auto ep_device =
      Singleton<ep::DeviceManagerRegistry>::Get()->GetDevice(device_type, device_index);
  ep::AllocationOptions options{};
  options.SetPinnedDevice(device_type, device_index);
  auto ep_backend_allocator = std::make_unique<EpBackendHostAllocator>(ep_device, options);
  auto bin_allo = std::make_unique<BinAllocator<ThreadSafeLock>>(ep::kMaxAlignmentRequirement,
                                                                 std::move(ep_backend_allocator));
  device_ctx->reset(new EpDeviceCtx(device, std::move(bin_allo)));
}

void PinnedEpStreamType::InitInstructionStatus(const Stream& stream,
                                               InstructionStatusBuffer* status_buffer) const {
  static_assert(sizeof(EpOptionalEventRecordStatusQuerier) < kInstructionStatusBufferBytes, "");
  auto* data_ptr = status_buffer->mut_buffer();
  EpOptionalEventRecordStatusQuerier::PlacementNew(data_ptr, nullptr);
}

void PinnedEpStreamType::DeleteInstructionStatus(const Stream& stream,
                                                 InstructionStatusBuffer* status_buffer) const {
  auto* ptr = EpOptionalEventRecordStatusQuerier::MutCast(status_buffer->mut_buffer());
  ptr->~EpOptionalEventRecordStatusQuerier();
}

bool PinnedEpStreamType::QueryInstructionStatusDone(
    const Stream& stream, const InstructionStatusBuffer& status_buffer) const {
  return EpOptionalEventRecordStatusQuerier::Cast(status_buffer.buffer())->done();
}

void PinnedEpStreamType::Run(Instruction* instruction) const {
  OF_PROFILER_RANGE_GUARD("S:" + instruction->DebugName());
  auto* stream = instruction->mut_stream();
  NaiveStreamPolicy* naive_stream_policy =
      dynamic_cast<NaiveStreamPolicy*>(instruction->mut_stream()->mut_stream_policy());
  CHECK_NOTNULL(naive_stream_policy);
  auto* ep_device_ctx = dynamic_cast<EpDeviceCtx*>(naive_stream_policy->device_ctx().get());
  auto* ep_device = ep_device_ctx->GetOrCreateEpDevice();
  ep_device->SetAsActiveDevice();
  instruction->Compute();
  char* data_ptr = instruction->mut_status_buffer()->mut_buffer();
  EpOptionalEventRecordStatusQuerier::MutCast(data_ptr)->SetLaunched(
      stream->mut_stream_policy()->stream());
}

}  // namespace vm
}  // namespace oneflow
