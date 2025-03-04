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
#include "oneflow/core/vm/touch_tensors_instruction_type.h"
#include "oneflow/core/eager/eager_blob_object.h"

namespace oneflow {
namespace vm {

TouchTensorsPhyInstrOperand::TouchTensorsPhyInstrOperand(
    const one::EagerBlobObjectList& eager_blob_objects)
    : eager_blob_objects_(eager_blob_objects) {
  const auto& Insert = SetInserter(&input_dependences_);
  for (const auto& eager_blob_object : eager_blob_objects_) {
    Insert(CHECK_JUST(eager_blob_object->compute_local_dep_object()));
  }
}

}  // namespace vm
}  // namespace oneflow
