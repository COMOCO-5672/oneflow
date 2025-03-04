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
#ifndef ONEFLOW_CORE_CCL_CCL_H_
#define ONEFLOW_CORE_CCL_CCL_H_

#include "oneflow/core/common/data_type.pb.h"
#include "oneflow/core/common/device_type.h"
#include "oneflow/core/common/symbol.h"
#include "oneflow/core/common/switch_func.h"
#include "oneflow/core/ep/include/stream.h"

namespace oneflow {

class ParallelDesc;
class TransportToken;

// collective communication library
namespace ccl {

#define CCL_REDUCE_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(kSum)

enum ReduceType {
  kInvalidReduceFunctorType = 0,
#define DEFINE_REDUCE_TYPE_ENUM_VALUE(enum_value) enum_value,
  OF_PP_FOR_EACH_TUPLE(DEFINE_REDUCE_TYPE_ENUM_VALUE, CCL_REDUCE_TYPE_SEQ)
#undef DEFINE_REDUCE_TYPE_ENUM_VALUE
      kReduceTypeSize
};

#define CCL_REDUCE_TYPE_CTRV_SEQ  \
  MAKE_TYPED_CTRV_SEQ(ReduceType, \
                      OF_PP_FOR_EACH_TUPLE(OF_PP_I_MAKE_REPLICATE_TUPLE_SEQ, CCL_REDUCE_TYPE_SEQ))

template<DeviceType device_type>
Maybe<void> AllReduce(const void* in, void* out, size_t elem_cnt, DataType dtype,
                      ReduceType reduce_type, Symbol<ParallelDesc> parallel_desc,
                      ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> ReduceScatter(const void* in, void* out, size_t elem_cnt, DataType dtype,
                          ReduceType reduce_type, Symbol<ParallelDesc> parallel_desc,
                          ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> AllGather(const void* in, void* out, size_t elem_cnt, DataType dtype,
                      Symbol<ParallelDesc> parallel_desc, ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> Send(const void* in, size_t elem_cnt, DataType dtype, int64_t dst, ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> Recv(void* out, size_t elem_cnt, DataType dtype, int64_t src, ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> Broadcast(const void* in, void* out, size_t elem_cnt, DataType dtype, int64_t root,
                      Symbol<ParallelDesc> parallel_desc, ep::Stream* stream);

template<DeviceType device_type>
Maybe<void> Reduce(const void* in, void* out, size_t elem_cnt, DataType dtype,
                   ReduceType reduce_type, int64_t root, Symbol<ParallelDesc> parallel_desc,
                   ep::Stream* stream);

Maybe<void> CpuBroadcast(const void* in, void* out, size_t buffer_size, int64_t root,
                         Symbol<ParallelDesc> parallel_desc, const TransportToken& transport_token);

}  // namespace ccl

}  // namespace oneflow

#endif  // ONEFLOW_CORE_CCL_CCL_H_
