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
#ifndef ONEFLOW_CORE_FRAMEWORK_STREAM_IS_COMM_NET_STREAM_H_
#define ONEFLOW_CORE_FRAMEWORK_STREAM_IS_COMM_NET_STREAM_H_

#include <glog/logging.h>
#include "oneflow/core/common/stream_role.h"

namespace oneflow {

struct IsCommNetStream final : public StreamRoleVisitor<IsCommNetStream> {
  static bool VisitCompute() { return false; }
  static bool VisitHost2Device() { return false; }
  static bool VisitDevice2Host() { return false; }
  static bool VisitSyncedLaunchedCommNet() { return true; }
  static bool VisitAsyncedLaunchedCommNet() { return true; }
  static bool VisitBarrier() { return false; }
  static bool VisitCriticalSection() { return false; }
  static bool VisitLazyJobLauncher() { return false; }
  static bool VisitPinnedCompute() { return VisitCompute(); }
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_FRAMEWORK_STREAM_IS_COMM_NET_STREAM_H_
