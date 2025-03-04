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
#include <limits>
#include "oneflow/core/device/cuda_util.h"
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/ndarray/ndarray_util.h"
#include "oneflow/core/ndarray/xpu_var_ndarray.h"
#include "oneflow/core/kernel/kernel_util.h"
#include "oneflow/core/kernel/cuda_graph_support.h"
#include "oneflow/core/ep/include/primitive/cast.h"
#include "oneflow/core/ep/include/primitive/fill.h"
#include "oneflow/core/ep/cuda/cuda_device.h"
#include "oneflow/core/ep/include/primitive/matmul.h"
#include "oneflow/user/kernels/fused_rnn_cell_kernel_util.h"

// NOTE(Liang Depeng): The implementation of fused_gru_cell is modified from
//                     https://github.com/pytorch/pytorch/blob/master/aten/src/ATen/native/cuda/RNN.cu

namespace oneflow {

namespace {

template<typename T>
struct AccumulateType {};
template<>
struct AccumulateType<float> {
  using type = float;
};
template<>
struct AccumulateType<double> {
  using type = double;
};

template<typename T>
using acc_type = typename AccumulateType<T>::type;

#define H2F(input) static_cast<ACC_T>(input)
#define F2H(input) static_cast<T>(input)

template<typename T>
__device__ __forceinline__ T sigmoid(T in) {
  T one = static_cast<T>(1.0);
  return one / (one + ::exp(-in));
}

template<typename T, typename ACC_T, typename IDX_TYPE>
#if __CUDA_ARCH__ >= 350
OF_LAUNCH_BOUNDS_2(512, 4)
#endif
__global__ void gru_cell_forward(const IDX_TYPE numel, const IDX_TYPE hidden_size,
                                 const T* input_gates_ptr, const T* hidden_gates_ptr,
                                 const T* hx_ptr, const T* input_bias_ptr, const T* hidden_bias_ptr,
                                 T* hy_ptr, T* workspace_ptr) {
  bool has_bias = input_bias_ptr != nullptr;
  for (IDX_TYPE linearIndex = blockIdx.x * blockDim.x + threadIdx.x; linearIndex < numel;
       linearIndex += gridDim.x * blockDim.x) {
    IDX_TYPE offset = (linearIndex / hidden_size) * 3 * hidden_size + linearIndex % hidden_size;

    T ir = input_gates_ptr[offset + 0 * hidden_size];
    T ii = input_gates_ptr[offset + 1 * hidden_size];
    T in = input_gates_ptr[offset + 2 * hidden_size];
    T hr = hidden_gates_ptr[offset + 0 * hidden_size];
    T hi = hidden_gates_ptr[offset + 1 * hidden_size];
    T hn = hidden_gates_ptr[offset + 2 * hidden_size];

    T hx = hx_ptr[linearIndex];
    T* hy = &(hy_ptr[linearIndex]);

    T b1r, b1i, b1n, b2r, b2i, b2n;

    if (has_bias) {
      b1r = input_bias_ptr[linearIndex % hidden_size + 0 * hidden_size];
      b1i = input_bias_ptr[linearIndex % hidden_size + 1 * hidden_size];
      b1n = input_bias_ptr[linearIndex % hidden_size + 2 * hidden_size];

      b2r = hidden_bias_ptr[linearIndex % hidden_size + 0 * hidden_size];
      b2i = hidden_bias_ptr[linearIndex % hidden_size + 1 * hidden_size];
      b2n = hidden_bias_ptr[linearIndex % hidden_size + 2 * hidden_size];
    } else {
      b1r = F2H(0.0);
      b1i = F2H(0.0);
      b1n = F2H(0.0);
      b2r = F2H(0.0);
      b2i = F2H(0.0);
      b2n = F2H(0.0);
    }

    offset = (linearIndex / hidden_size) * 5 * hidden_size + linearIndex % hidden_size;
    ACC_T rg, ig, ng;
    rg = sigmoid(H2F(ir) + H2F(hr) + H2F(b1r) + H2F(b2r));
    ig = sigmoid(H2F(ii) + H2F(hi) + H2F(b1i) + H2F(b2i));

    ng = H2F(in) + H2F(b1n) + rg * (H2F(hn) + H2F(b2n));
    ng = ::tanh(ng);
    *hy = F2H(ng + ig * (H2F(hx) - ng));

    // SAVE FOR BACKWARDS
    workspace_ptr[offset + 0 * hidden_size] = F2H(rg);
    workspace_ptr[offset + 1 * hidden_size] = F2H(ig);
    workspace_ptr[offset + 2 * hidden_size] = F2H(ng);
    workspace_ptr[offset + 3 * hidden_size] = hx;
    workspace_ptr[offset + 4 * hidden_size] = F2H(H2F(hn) + H2F(b2n));
  }
}

template<typename T, typename ACC_T, typename IDX_TYPE>
#if __CUDA_ARCH__ >= 350
OF_LAUNCH_BOUNDS_2(512, 4)
#endif
__global__
    void gru_cell_backward(const IDX_TYPE numel, const IDX_TYPE hidden_size, const T* grad_hy_ptr,
                           const T* workspace_ptr, T* grad_input_gates_ptr,
                           T* grad_hidden_gates_ptr, T* grad_hx_ptr) {
  for (IDX_TYPE linearIndex = blockIdx.x * blockDim.x + threadIdx.x; linearIndex < numel;
       linearIndex += gridDim.x * blockDim.x) {
    IDX_TYPE offset = (linearIndex / hidden_size) * 5 * hidden_size + linearIndex % hidden_size;

    T rg = workspace_ptr[offset + 0 * hidden_size];
    T ig = workspace_ptr[offset + 1 * hidden_size];
    T ng = workspace_ptr[offset + 2 * hidden_size];
    T hx = workspace_ptr[offset + 3 * hidden_size];
    T hn = workspace_ptr[offset + 4 * hidden_size];

    T go = grad_hy_ptr[linearIndex];

    offset = (linearIndex / hidden_size) * 3 * hidden_size + linearIndex % hidden_size;

    ACC_T gig = H2F(go) * (H2F(hx) - H2F(ng)) * (1 - H2F(ig)) * H2F(ig);
    ACC_T ghx = H2F(go) * H2F(ig);
    ACC_T gin = H2F(go) * (1 - H2F(ig)) * (1 - H2F(ng) * H2F(ng));
    ACC_T ghn = gin * H2F(rg);
    ACC_T grg = gin * H2F(hn) * (1 - H2F(rg)) * H2F(rg);

    grad_input_gates_ptr[offset + 0 * hidden_size] = F2H(grg);
    grad_input_gates_ptr[offset + 1 * hidden_size] = F2H(gig);
    grad_input_gates_ptr[offset + 2 * hidden_size] = F2H(gin);

    grad_hidden_gates_ptr[offset + 0 * hidden_size] = F2H(grg);
    grad_hidden_gates_ptr[offset + 1 * hidden_size] = F2H(gig);
    grad_hidden_gates_ptr[offset + 2 * hidden_size] = F2H(ghn);
    if (grad_hx_ptr != nullptr) { grad_hx_ptr[linearIndex] = F2H(ghx); }
  }
}

template<typename T>
struct FusedGruCellGradFunctor final {
  void operator()(ep::Stream* stream, const int64_t hx_numel, const int64_t workspace_numel,
                  const int64_t hidden_size, const T* grad_hy_ptr, const T* workspace_ptr,
                  T* grad_input_gates_ptr, T* grad_hidden_gates_ptr, T* grad_hx_ptr) {
    using ACC_T = acc_type<T>;
    if (workspace_numel < std::numeric_limits<int32_t>::max()) {
      RUN_CUDA_KERNEL((gru_cell_backward<T, ACC_T, int32_t>), stream, hx_numel,
                      static_cast<int32_t>(hx_numel), static_cast<int32_t>(hidden_size),
                      grad_hy_ptr, workspace_ptr, grad_input_gates_ptr, grad_hidden_gates_ptr,
                      grad_hx_ptr);
    } else {
      RUN_CUDA_KERNEL((gru_cell_backward<T, ACC_T, int64_t>), stream, hx_numel, hx_numel,
                      hidden_size, grad_hy_ptr, workspace_ptr, grad_input_gates_ptr,
                      grad_hidden_gates_ptr, grad_hx_ptr);
    }
  }
};

template<>
void FusedGruCellGradFunctor<float16>::operator()(
    ep::Stream* stream, const int64_t hx_numel, const int64_t workspace_numel,
    const int64_t hidden_size, const float16* grad_hy_ptr, const float16* workspace_ptr,
    float16* grad_input_gates_ptr, float16* grad_hidden_gates_ptr, float16* grad_hx_ptr) {
  if (workspace_numel < std::numeric_limits<int32_t>::max()) {
    RUN_CUDA_KERNEL(
        (gru_cell_backward<half, float, int32_t>), stream, hx_numel, static_cast<int32_t>(hx_numel),
        static_cast<int32_t>(hidden_size), reinterpret_cast<const half*>(grad_hy_ptr),
        reinterpret_cast<const half*>(workspace_ptr), reinterpret_cast<half*>(grad_input_gates_ptr),
        reinterpret_cast<half*>(grad_hidden_gates_ptr), reinterpret_cast<half*>(grad_hx_ptr));
  } else {
    RUN_CUDA_KERNEL(
        (gru_cell_backward<half, float, int64_t>), stream, hx_numel, hx_numel, hidden_size,
        reinterpret_cast<const half*>(grad_hy_ptr), reinterpret_cast<const half*>(workspace_ptr),
        reinterpret_cast<half*>(grad_input_gates_ptr),
        reinterpret_cast<half*>(grad_hidden_gates_ptr), reinterpret_cast<half*>(grad_hx_ptr));
  }
}

template<typename T>
struct FusedGruCellFunctor final {
  void operator()(ep::Stream* stream, const int64_t hx_numel, const int64_t workspace_numel,
                  const int64_t hidden_size, const T* input_gates_ptr, const T* hidden_gates_ptr,
                  const T* hx_ptr, const T* input_bias_ptr, const T* hidden_bias_ptr, T* hy_ptr,
                  T* workspace_ptr) {
    using ACC_T = acc_type<T>;
    if (workspace_numel < std::numeric_limits<int32_t>::max()) {
      RUN_CUDA_KERNEL((gru_cell_forward<T, ACC_T, int32_t>), stream, hx_numel,
                      static_cast<int32_t>(hx_numel), static_cast<int32_t>(hidden_size),
                      input_gates_ptr, hidden_gates_ptr, hx_ptr, input_bias_ptr, hidden_bias_ptr,
                      hy_ptr, workspace_ptr);
    } else {
      RUN_CUDA_KERNEL((gru_cell_forward<T, ACC_T, int64_t>), stream, hx_numel, hx_numel,
                      hidden_size, input_gates_ptr, hidden_gates_ptr, hx_ptr, input_bias_ptr,
                      hidden_bias_ptr, hy_ptr, workspace_ptr);
    }
  }
};

template<>
void FusedGruCellFunctor<float16>::operator()(
    ep::Stream* stream, const int64_t hx_numel, const int64_t workspace_numel,
    const int64_t hidden_size, const float16* input_gates_ptr, const float16* hidden_gates_ptr,
    const float16* hx_ptr, const float16* input_bias_ptr, const float16* hidden_bias_ptr,
    float16* hy_ptr, float16* workspace_ptr) {
  if (workspace_numel < std::numeric_limits<int32_t>::max()) {
    RUN_CUDA_KERNEL(
        (gru_cell_forward<half, float, int32_t>), stream, hx_numel, static_cast<int32_t>(hx_numel),
        static_cast<int32_t>(hidden_size), reinterpret_cast<const half*>(input_gates_ptr),
        reinterpret_cast<const half*>(hidden_gates_ptr), reinterpret_cast<const half*>(hx_ptr),
        reinterpret_cast<const half*>(input_bias_ptr),
        reinterpret_cast<const half*>(hidden_bias_ptr), reinterpret_cast<half*>(hy_ptr),
        reinterpret_cast<half*>(workspace_ptr));
  } else {
    RUN_CUDA_KERNEL((gru_cell_forward<half, float, int64_t>), stream, hx_numel, hx_numel,
                    hidden_size, reinterpret_cast<const half*>(input_gates_ptr),
                    reinterpret_cast<const half*>(hidden_gates_ptr),
                    reinterpret_cast<const half*>(hx_ptr),
                    reinterpret_cast<const half*>(input_bias_ptr),
                    reinterpret_cast<const half*>(hidden_bias_ptr), reinterpret_cast<half*>(hy_ptr),
                    reinterpret_cast<half*>(workspace_ptr));
  }
}

}  // namespace

template<typename T>
class GpuFusedGruCellKernel final : public user_op::OpKernel {
 public:
  GpuFusedGruCellKernel() = default;
  ~GpuFusedGruCellKernel() = default;

 private:
  using user_op::OpKernel::Compute;
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* input_gates = ctx->Tensor4ArgNameAndIndex("input_gates", 0);
    const user_op::Tensor* hidden_gates = ctx->Tensor4ArgNameAndIndex("hidden_gates", 0);
    const user_op::Tensor* hx = ctx->Tensor4ArgNameAndIndex("hx", 0);
    user_op::Tensor* hy = ctx->Tensor4ArgNameAndIndex("hy", 0);
    user_op::Tensor* workspace = ctx->Tensor4ArgNameAndIndex("workspace", 0);

    const T* input_bias_ptr = nullptr;
    const T* hidden_bias_ptr = nullptr;
    if (ctx->has_input("input_bias", 0)) {
      CHECK(ctx->has_input("hidden_bias", 0));
      input_bias_ptr = ctx->Tensor4ArgNameAndIndex("input_bias", 0)->dptr<T>();
      hidden_bias_ptr = ctx->Tensor4ArgNameAndIndex("hidden_bias", 0)->dptr<T>();
    }
    const T* input_gates_ptr = input_gates->dptr<T>();
    const T* hidden_gates_ptr = hidden_gates->dptr<T>();
    const T* hx_ptr = hx->dptr<T>();

    T* hy_ptr = hy->mut_dptr<T>();
    T* workspace_ptr = workspace->mut_dptr<T>();
    const int64_t hx_numel = hx->shape_view().elem_cnt();
    const int64_t workspace_numel = workspace->shape_view().elem_cnt();
    const int64_t hidden_size = hx->shape_view().At(hx->shape_view().NumAxes() - 1);
    FusedGruCellFunctor<T>()(ctx->stream(), hx_numel, workspace_numel, hidden_size, input_gates_ptr,
                             hidden_gates_ptr, hx_ptr, input_bias_ptr, hidden_bias_ptr, hy_ptr,
                             workspace_ptr);
  }

  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_FUSED_GRU_CELL_KERNEL(dtype)                                                   \
  REGISTER_USER_KERNEL("fused_gru_cell")                                                        \
      .SetCreateFn<GpuFusedGruCellKernel<dtype>>()                                              \
      .SetIsMatchedHob((user_op::HobDeviceType() == DeviceType::kCUDA)                          \
                       && (user_op::HobDataType("hx", 0) == GetDataType<dtype>::value)          \
                       && (user_op::HobDataType("input_gates", 0) == GetDataType<dtype>::value) \
                       && (user_op::HobDataType("hidden_gates", 0) == GetDataType<dtype>::value))

REGISTER_FUSED_GRU_CELL_KERNEL(float);
REGISTER_FUSED_GRU_CELL_KERNEL(float16);

class GpuFusedGruCellGradFloatKernel final : public user_op::OpKernel {
 public:
  GpuFusedGruCellGradFloatKernel() = default;
  ~GpuFusedGruCellGradFloatKernel() = default;

 private:
  using user_op::OpKernel::Compute;
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* grad_hy = ctx->Tensor4ArgNameAndIndex("grad_hy", 0);
    const user_op::Tensor* workspace = ctx->Tensor4ArgNameAndIndex("workspace", 0);
    user_op::Tensor* grad_input_gates = ctx->Tensor4ArgNameAndIndex("grad_input_gates", 0);
    user_op::Tensor* grad_hidden_gates = ctx->Tensor4ArgNameAndIndex("grad_hidden_gates", 0);

    const float* grad_hy_ptr = grad_hy->dptr<float>();
    const float* workspace_ptr = workspace->dptr<float>();

    float* grad_input_gates_ptr = grad_input_gates->mut_dptr<float>();
    float* grad_hidden_gates_ptr = grad_hidden_gates->mut_dptr<float>();

    float* grad_hx_ptr = nullptr;
    if (ctx->has_output("grad_hx", 0)) {
      user_op::Tensor* grad_hx = ctx->Tensor4ArgNameAndIndex("grad_hx", 0);
      grad_hx_ptr = grad_hx->mut_dptr<float>();
    }

    const int64_t hx_numel = grad_hy->shape_view().elem_cnt();
    const int64_t workspace_numel = workspace->shape_view().elem_cnt();
    const int64_t hidden_size = grad_hy->shape_view().At(grad_hy->shape_view().NumAxes() - 1);
    FusedGruCellGradFunctor<float>()(ctx->stream(), hx_numel, workspace_numel, hidden_size,
                                     grad_hy_ptr, workspace_ptr, grad_input_gates_ptr,
                                     grad_hidden_gates_ptr, grad_hx_ptr);

    if (ctx->has_output("grad_input_bias", 0) && ctx->has_output("grad_hidden_bias", 0)) {
      float* grad_input_bias_ptr =
          ctx->Tensor4ArgNameAndIndex("grad_input_bias", 0)->mut_dptr<float>();
      std::vector<int32_t> axis;
      axis.push_back(0);
      const Shape& reduced_shape =
          CreateReducedShape(grad_input_gates->shape_view(), {axis.begin(), axis.end()});
      user_op::Tensor* tmp_buffer = ctx->Tensor4ArgNameAndIndex("tmp_buffer", 0);
      NdarrayReduce<DeviceType::kCUDA, float, BinaryFuncSum>::Reduce(
          ctx->stream(), XpuVarNdarray<float>(reduced_shape, grad_input_bias_ptr),
          XpuVarNdarray<const float>(grad_input_gates->shape_view(),
                                     grad_input_gates->dptr<float>()),
          XpuVarNdarray<float>(tmp_buffer->shape_view(), tmp_buffer->mut_dptr<float>()));

      float* grad_hidden_bias_ptr =
          ctx->Tensor4ArgNameAndIndex("grad_hidden_bias", 0)->mut_dptr<float>();
      NdarrayReduce<DeviceType::kCUDA, float, BinaryFuncSum>::Reduce(
          ctx->stream(), XpuVarNdarray<float>(reduced_shape, grad_hidden_bias_ptr),
          XpuVarNdarray<const float>(grad_hidden_gates->shape_view(),
                                     grad_hidden_gates->dptr<float>()),
          XpuVarNdarray<float>(tmp_buffer->shape_view(), tmp_buffer->mut_dptr<float>()));
    }
  }

  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

REGISTER_USER_KERNEL("fused_gru_cell_grad")
    .SetCreateFn<GpuFusedGruCellGradFloatKernel>()
    .SetIsMatchedHob((user_op::HobDeviceType() == DeviceType::kCUDA)
                     && (user_op::HobDataType("grad_hy", 0) == GetDataType<float>::value)
                     && (user_op::HobDataType("workspace", 0) == GetDataType<float>::value))
    .SetInferTmpSizeFn([](user_op::InferContext* ctx) {
      size_t tmp_bytes = 0;
      if (ctx->has_output("grad_input_bias", 0) && ctx->has_output("grad_hidden_bias", 0)) {
        const Shape& in_shape = ctx->InputTensorDesc("grad_hy", 0).shape();
        tmp_bytes = GetCudaAlignedSize(in_shape.elem_cnt() * 3 * sizeof(float));
      } else {
        tmp_bytes = 0;
      }
      return tmp_bytes;
    });

class GpuFusedGruCellGradHalfKernel final : public user_op::OpKernel {
 public:
  GpuFusedGruCellGradHalfKernel() = default;
  ~GpuFusedGruCellGradHalfKernel() = default;

 private:
  using user_op::OpKernel::Compute;
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* grad_hy = ctx->Tensor4ArgNameAndIndex("grad_hy", 0);
    const user_op::Tensor* workspace = ctx->Tensor4ArgNameAndIndex("workspace", 0);
    user_op::Tensor* grad_input_gates = ctx->Tensor4ArgNameAndIndex("grad_input_gates", 0);
    user_op::Tensor* grad_hidden_gates = ctx->Tensor4ArgNameAndIndex("grad_hidden_gates", 0);

    const float16* grad_hy_ptr = grad_hy->dptr<float16>();
    const float16* workspace_ptr = workspace->dptr<float16>();

    float16* grad_input_gates_ptr = grad_input_gates->mut_dptr<float16>();
    float16* grad_hidden_gates_ptr = grad_hidden_gates->mut_dptr<float16>();

    float16* grad_hx_ptr = nullptr;
    if (ctx->has_output("grad_hx", 0)) {
      user_op::Tensor* grad_hx = ctx->Tensor4ArgNameAndIndex("grad_hx", 0);
      grad_hx_ptr = grad_hx->mut_dptr<float16>();
    }

    const int64_t hx_numel = grad_hy->shape_view().elem_cnt();
    const int64_t workspace_numel = workspace->shape_view().elem_cnt();
    const int64_t hidden_size = grad_hy->shape_view().At(grad_hy->shape_view().NumAxes() - 1);
    FusedGruCellGradFunctor<float16>()(ctx->stream(), hx_numel, workspace_numel, hidden_size,
                                       grad_hy_ptr, workspace_ptr, grad_input_gates_ptr,
                                       grad_hidden_gates_ptr, grad_hx_ptr);

    if (ctx->has_output("grad_input_bias", 0) && ctx->has_output("grad_hidden_bias", 0)) {
      std::vector<int32_t> axis;
      axis.push_back(0);
      user_op::Tensor* tmp_buffer = ctx->Tensor4ArgNameAndIndex("tmp_buffer", 0);
      const ShapeView& in_shape = grad_input_gates->shape_view();
      const Shape& reduced_shape = CreateReducedShape(in_shape, {axis.begin(), axis.end()});
      float* in_tmp_buffer = tmp_buffer->mut_dptr<float>();
      const size_t in_tmp_buffer_bytes = GetCudaAlignedSize(in_shape.elem_cnt() * sizeof(float));
      float* out_tmp_buffer =
          reinterpret_cast<float*>(tmp_buffer->mut_dptr<char>() + in_tmp_buffer_bytes);
      const size_t out_tmp_buffer_bytes =
          GetCudaAlignedSize(reduced_shape.elem_cnt() * sizeof(float));
      float* reduce_tmp_buffer = reinterpret_cast<float*>(
          tmp_buffer->mut_dptr<char>() + in_tmp_buffer_bytes + out_tmp_buffer_bytes);
      const size_t reduce_tmp_buffer_bytes =
          GetCudaAlignedSize(in_shape.elem_cnt() * sizeof(float));
      CHECK_LE(in_tmp_buffer_bytes + out_tmp_buffer_bytes + reduce_tmp_buffer_bytes,
               tmp_buffer->shape_view().elem_cnt());
      auto h2f = ep::primitive::NewPrimitive<ep::primitive::CastFactory>(
          ctx->device_type(), DataType::kFloat16, DataType::kFloat);
      CHECK(h2f);
      auto f2h = ep::primitive::NewPrimitive<ep::primitive::CastFactory>(
          ctx->device_type(), DataType::kFloat, DataType::kFloat16);
      CHECK(f2h);
      h2f->Launch(ctx->stream(), grad_input_gates->dptr<float16>(), in_tmp_buffer,
                  in_shape.elem_cnt());

      NdarrayReduce<DeviceType::kCUDA, float, BinaryFuncSum>::Reduce(
          ctx->stream(), XpuVarNdarray<float>(reduced_shape, out_tmp_buffer),
          XpuVarNdarray<const float>(in_shape, in_tmp_buffer),
          XpuVarNdarray<float>(in_shape, reduce_tmp_buffer));

      user_op::Tensor* output_tensor = ctx->Tensor4ArgNameAndIndex("grad_input_bias", 0);
      f2h->Launch(ctx->stream(), out_tmp_buffer, output_tensor->mut_dptr<float16>(),
                  output_tensor->shape_view().elem_cnt());

      h2f->Launch(ctx->stream(), grad_hidden_gates->dptr<float16>(), in_tmp_buffer,
                  in_shape.elem_cnt());
      NdarrayReduce<DeviceType::kCUDA, float, BinaryFuncSum>::Reduce(
          ctx->stream(), XpuVarNdarray<float>(reduced_shape, out_tmp_buffer),
          XpuVarNdarray<const float>(in_shape, in_tmp_buffer),
          XpuVarNdarray<float>(in_shape, reduce_tmp_buffer));

      output_tensor = ctx->Tensor4ArgNameAndIndex("grad_hidden_bias", 0);
      f2h->Launch(ctx->stream(), out_tmp_buffer, output_tensor->mut_dptr<float16>(),
                  output_tensor->shape_view().elem_cnt());
    }
  }

  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

REGISTER_USER_KERNEL("fused_gru_cell_grad")
    .SetCreateFn<GpuFusedGruCellGradHalfKernel>()
    .SetIsMatchedHob((user_op::HobDeviceType() == DeviceType::kCUDA)
                     && (user_op::HobDataType("grad_hy", 0) == GetDataType<float16>::value)
                     && (user_op::HobDataType("workspace", 0) == GetDataType<float16>::value))
    .SetInferTmpSizeFn([](user_op::InferContext* ctx) {
      size_t tmp_bytes = 0;
      if (ctx->has_output("grad_input_bias", 0) && ctx->has_output("grad_hidden_bias", 0)) {
        const Shape& in_shape = ctx->InputTensorDesc("grad_hy", 0).shape();
        const Shape& out_shape = ctx->OutputTensorDesc("grad_input_bias", 0)->shape();
        tmp_bytes = (2 * GetCudaAlignedSize(in_shape.elem_cnt() * 3 * sizeof(float))
                     + GetCudaAlignedSize(out_shape.elem_cnt() * sizeof(float)));
      } else {
        tmp_bytes = 0;
      }
      return tmp_bytes;
    });

}  // namespace oneflow
