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
#ifndef ONEFLOW_API_PYTHON_JOB_BUILD_JOB_BUILD_AND_INFER_H_
#define ONEFLOW_API_PYTHON_JOB_BUILD_JOB_BUILD_AND_INFER_H_

#include "oneflow/core/job/global_for.h"
#include "oneflow/core/common/protobuf.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/tensor_name_scope.h"
#include "oneflow/core/job/job_build_and_infer_ctx.h"
#include "oneflow/core/job/job_build_and_infer_ctx_mgr.h"
#include "oneflow/core/job/job.pb.h"
#include "oneflow/core/job/lazy_mode.h"
#include "oneflow/core/record/record.pb.h"

namespace oneflow {

inline Maybe<void> JobBuildAndInferCtx_Open(const std::string& job_name) {
  auto* mgr = JUST(GlobalJobBuildAndInferCtxMgr());
  return mgr->OpenJobBuildAndInferCtx(job_name);
}

inline Maybe<std::string> JobBuildAndInferCtx_GetCurrentJobName() {
  auto* mgr = JUST(GlobalJobBuildAndInferCtxMgr());
  return mgr->GetCurrentJobName();
}

inline Maybe<int64_t> JobBuildAndInferCtx_GetCurrentJobId() {
  return JUST(GetCurInferCtx())->job_id();
}

inline Maybe<void> JobBuildAndInferCtx_Close() {
  auto* mgr = JUST(GlobalJobBuildAndInferCtxMgr());
  JUST(mgr->CloseCurrentJobBuildAndInferCtx());
  return Maybe<void>::Ok();
}

inline Maybe<void> CurJobBuildAndInferCtx_CheckJob() { return JUST(GetCurInferCtx())->CheckJob(); }

inline Maybe<void> CurJobBuildAndInferCtx_SetJobConf(const std::string& job_conf_str) {
  JobConfigProto job_conf;
  CHECK_OR_RETURN(TxtString2PbMessage(job_conf_str, &job_conf)) << "job conf parse failed";
  return JUST(GetCurInferCtx())->SetJobConf(job_conf);
}

inline Maybe<void> CurJobBuildAndInferCtx_SetTrainConf(const std::string& train_conf_str) {
  TrainConf train_conf;
  CHECK_OR_RETURN(TxtString2PbMessage(train_conf_str, &train_conf)) << "train conf parse failed";
  return JUST(GetCurInferCtx())->SetTrainConf(train_conf);
}

inline Maybe<void> CurJobBuildAndInferCtx_Complete() { return JUST(GetCurInferCtx())->Complete(); }
inline Maybe<void> CurJobBuildAndInferCtx_Rebuild() { return JUST(GetCurInferCtx())->Rebuild(); }

inline Maybe<bool> CurJobBuildAndInferCtx_HasJobConf() {
  return JUST(GetCurInferCtx())->HasJobConf();
}

inline Maybe<std::string> CurJobBuildAndInferCtx_AddAndInferLocalOp(
    const std::string& op_conf_str) {
  OperatorConf op_conf;
  CHECK_OR_RETURN(TxtString2PbMessage(op_conf_str, &op_conf)) << "operator conf parse failed";
  auto* ctx = JUST(GetCurInferCtx());
  const auto& op_attribute = JUST(ctx->AddAndInferLocalOp(op_conf));
  return PbMessage2TxtString(*op_attribute);
}

inline Maybe<std::string> CurJobBuildAndInferCtx_AddAndInferGlobalOp(
    const std::string& op_conf_str) {
  OperatorConf op_conf;
  CHECK_OR_RETURN(TxtString2PbMessage(op_conf_str, &op_conf)) << "operator conf parse failed";
  auto* ctx = JUST(GetCurInferCtx());
  const auto& op_attribute = JUST(ctx->AddAndInferGlobalOp(op_conf));
  return PbMessage2TxtString(*op_attribute);
}

inline Maybe<void> CurJobBuildAndInferCtx_AddLbiAndDiffWatcherUuidPair(
    const std::string& lbi_uuid_pair_str) {
  auto* ctx = JUST(GetCurInferCtx());
  LbiAndDiffWatcherUuidPair lbi_uuid_pair;
  CHECK_OR_RETURN(TxtString2PbMessage(lbi_uuid_pair_str, &lbi_uuid_pair))
      << "LbiAndDiffWatcherUuidPair parse failed";
  return ctx->AddLbiAndDiffWatcherUuidPair(lbi_uuid_pair);
}

inline Maybe<std::string> JobBuildAndInferCtx_GetSerializedIdListAsStaticShape(
    const std::string& job_name, const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  const auto& shape = JUST(ctx->GetStaticShape(lbn));
  Int64List id_list;
  *id_list.mutable_value() = {shape->dim_vec().begin(), shape->dim_vec().end()};
  return PbMessage2TxtString(id_list);
}

inline Maybe<long long> JobBuildAndInferCtx_GetDataType(const std::string& job_name,
                                                        const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return JUST(ctx->GetDataType(lbn));
}

inline Maybe<bool> JobBuildAndInferCtx_IsDynamic(const std::string& job_name,
                                                 const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return ctx->IsDynamic(lbn);
}

inline Maybe<bool> JobBuildAndInferCtx_IsDisableBoxing(const std::string& job_name,
                                                       const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return ctx->IsDisableBoxing(lbn);
}

inline Maybe<std::string> JobBuildAndInferCtx_GetSplitAxisFromProducerView(
    const std::string& job_name, const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return PbMessage2TxtString(*JUST(ctx->GetSplitAxisFromProducerView(lbn)));
}

inline Maybe<std::string> JobBuildAndInferCtx_GetSerializedParallelConfFromProducerView(
    const std::string& job_name, const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return PbMessage2TxtString(JUST(ctx->GetParallelDescFromProducerView(lbn))->parallel_conf());
}

inline Maybe<void> CurJobBuildAndInferCtx_AddLossLogicalBlobName(const std::string& lbn) {
  return JUST(GetCurInferCtx())->AddLossLogicalBlobName(lbn);
}

inline Maybe<bool> JobBuildAndInferCtx_IsLocalBlob(const std::string& job_name,
                                                   const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return ctx->IsLocalBlob(lbn);
}

inline Maybe<int> JobBuildAndInferCtx_LocalBlobGetNumSubLbi(const std::string& job_name,
                                                            const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return ctx->LocalBlobGetNumSubLbi(lbn);
}

inline Maybe<std::string> JobBuildAndInferCtx_LocalBlobGetSubLbi(const std::string& job_name,
                                                                 const std::string& lbn,
                                                                 int index) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return PbMessage2TxtString(*JUST(ctx->LocalBlobGetSubLbi(lbn, index)));
}

inline Maybe<void> JobBuildAndInferCtx_CheckLbnValidAndExist(const std::string& job_name,
                                                             const std::string& lbn) {
  auto* ctx = JUST(GetJobBuildAndInferCtx(job_name));
  JUST(ctx->CheckLbnValidAndExist(lbn));
  return Maybe<void>::Ok();
}

inline Maybe<std::string> JobBuildAndInferCtx_GetOpBlobLbn(const std::string& job_name,
                                                           const std::string& op_name,
                                                           const std::string bn_in_op) {
  const auto* job_ctx = JUST(GetJobBuildAndInferCtx(job_name));
  return job_ctx->GetOpBlobLbn(op_name, bn_in_op);
}

inline Maybe<void> AddTensorAsGraphLoss(const std::shared_ptr<one::Tensor>& t) {
  CHECK_OR_RETURN(t->is_lazy());
  CHECK_OR_RETURN(LazyMode::is_enabled());
  const std::string& loss_lbn = one::TensorNameScope::Global()->Lookup(t);
  CHECK_OR_RETURN("" != loss_lbn);
  return JUST(GetCurInferCtx())->AddLossLogicalBlobName(loss_lbn);
}

}  // namespace oneflow

#endif  // ONEFLOW_API_PYTHON_JOB_BUILD_JOB_BUILD_AND_INFER_H_
