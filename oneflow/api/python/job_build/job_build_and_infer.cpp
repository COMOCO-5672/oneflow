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
#include <pybind11/pybind11.h>
#include <string>
#include "oneflow/api/python/of_api_registry.h"
#include "oneflow/api/python/job_build/job_build_and_infer.h"

namespace py = pybind11;

namespace oneflow {

ONEFLOW_API_PYBIND11_MODULE("", m) {
  m.def("JobBuildAndInferCtx_Open", &JobBuildAndInferCtx_Open);
  m.def("JobBuildAndInferCtx_GetCurrentJobName", &JobBuildAndInferCtx_GetCurrentJobName);
  m.def("JobBuildAndInferCtx_GetCurrentJobId", &JobBuildAndInferCtx_GetCurrentJobId);
  m.def("JobBuildAndInferCtx_Close", &JobBuildAndInferCtx_Close);

  m.def("CurJobBuildAndInferCtx_CheckJob", &CurJobBuildAndInferCtx_CheckJob);
  m.def("CurJobBuildAndInferCtx_SetJobConf", &CurJobBuildAndInferCtx_SetJobConf);
  m.def("CurJobBuildAndInferCtx_SetTrainConf", &CurJobBuildAndInferCtx_SetTrainConf);

  m.def("CurJobBuildAndInferCtx_Complete", &CurJobBuildAndInferCtx_Complete,
        py::call_guard<py::gil_scoped_release>());
  m.def("CurJobBuildAndInferCtx_Rebuild", &CurJobBuildAndInferCtx_Rebuild,
        py::call_guard<py::gil_scoped_release>());
  m.def("CurJobBuildAndInferCtx_HasJobConf", &CurJobBuildAndInferCtx_HasJobConf);
  m.def("CurJobBuildAndInferCtx_AddAndInferLocalOp", &CurJobBuildAndInferCtx_AddAndInferLocalOp,
        py::call_guard<py::gil_scoped_release>());

  m.def("CurJobBuildAndInferCtx_AddAndInferGlobalOp", &CurJobBuildAndInferCtx_AddAndInferGlobalOp);
  m.def("CurJobBuildAndInferCtx_AddLbiAndDiffWatcherUuidPair",
        &CurJobBuildAndInferCtx_AddLbiAndDiffWatcherUuidPair);

  m.def("JobBuildAndInferCtx_GetSerializedIdListAsStaticShape",
        &JobBuildAndInferCtx_GetSerializedIdListAsStaticShape);
  m.def("JobBuildAndInferCtx_GetDataType", &JobBuildAndInferCtx_GetDataType);
  m.def("JobBuildAndInferCtx_IsDynamic", &JobBuildAndInferCtx_IsDynamic);

  m.def("JobBuildAndInferCtx_IsDisableBoxing", &JobBuildAndInferCtx_IsDisableBoxing);

  m.def("JobBuildAndInferCtx_GetSplitAxisFromProducerView",
        &JobBuildAndInferCtx_GetSplitAxisFromProducerView);
  m.def("JobBuildAndInferCtx_GetSerializedParallelConfFromProducerView",
        &JobBuildAndInferCtx_GetSerializedParallelConfFromProducerView);

  m.def("CurJobBuildAndInferCtx_AddLossLogicalBlobName",
        &CurJobBuildAndInferCtx_AddLossLogicalBlobName);

  m.def("JobBuildAndInferCtx_IsLocalBlob", &JobBuildAndInferCtx_IsLocalBlob);
  m.def("JobBuildAndInferCtx_LocalBlobGetNumSubLbi", &JobBuildAndInferCtx_LocalBlobGetNumSubLbi);
  m.def("JobBuildAndInferCtx_LocalBlobGetSerializedSubLbi",
        &JobBuildAndInferCtx_LocalBlobGetSubLbi);
  m.def("JobBuildAndInferCtx_CheckLbnValidAndExist", &JobBuildAndInferCtx_CheckLbnValidAndExist);
  m.def("JobBuildAndInferCtx_GetOpBlobLbn", &JobBuildAndInferCtx_GetOpBlobLbn);
}

}  // namespace oneflow
