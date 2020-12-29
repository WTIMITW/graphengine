/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hybrid/executor/worker/task_compile_engine.h"
#include "init/gelib.h"
#include "hybrid/node_executor/node_executor.h"

namespace ge {
namespace hybrid {
Status TaskCompileEngine::Compile(NodeState &node_state, GraphExecutionContext *context) {
  const auto &node_item = *node_state.GetNodeItem();
  GE_CHECK_NOTNULL(context);
  RECORD_COMPILE_EVENT(context, node_item.NodeName().c_str(), "[Compile] Start");
  GE_CHK_RT_RET(rtCtxSetCurrent(context->rt_gen_context));

  if (context->ge_context != nullptr) {
    GetThreadLocalContext() = *context->ge_context;
  }
  shared_ptr<NodeTask> kernel_task;
  auto ret = node_item.node_executor->CompileTask(*context->model, node_item.node, kernel_task);
  RECORD_COMPILE_EVENT(context, node_state.GetName().c_str(), "[Compile] End");
  GE_CHK_STATUS_RET(ret, "Failed to create task for node: %s", node_item.NodeName().c_str());
  node_state.SetKernelTask(kernel_task);
  GELOGI("Compiling node %s successfully", node_state.GetName().c_str());
  return SUCCESS;
}
}  // namespace hybrid
}  // namespace ge