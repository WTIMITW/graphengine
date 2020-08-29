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

#include "rts_node_executor.h"
#include "common/debug/log.h"
#include "common/ge/ge_util.h"
#include "graph/utils/tensor_utils.h"
#include "runtime/rt.h"

namespace ge {
namespace hybrid {
REGISTER_NODE_EXECUTOR_BUILDER(NodeExecutorManager::ExecutorType::RTS, RtsNodeExecutor);

Status IdentityNodeTask::DoCopyTensor(TaskContext &context, int index) {
  auto input_desc = context.MutableInputDesc(index);
  GE_CHECK_NOTNULL(input_desc);
  int64_t copy_size = 0;
  GE_CHK_GRAPH_STATUS_RET(TensorUtils::GetTensorSizeInBytes(*input_desc, copy_size));
  // copy_size would not be negative since GetTensorSizeInBytes returned successfully.
  if (copy_size != 0) {
    GELOGD("[%s] index = %d, copy size = %ld", context.GetNodeName(), index, copy_size);
    auto input = context.MutableInput(index);
    auto output = context.MutableOutput(index);
    GE_CHECK_NOTNULL(input);
    GE_CHECK_NOTNULL(output);
    GE_CHK_RT_RET(rtMemcpyAsync(output->MutableData(), output->GetSize(), input->GetData(), copy_size,
                                RT_MEMCPY_DEVICE_TO_DEVICE, context.GetStream()));
  } else {
    GELOGW("[%s] index = %d, copy size = 0", context.GetNodeName(), index);
  }

  return SUCCESS;
}

Status IdentityNodeTask::ExecuteAsync(TaskContext &context, std::function<void()> done_callback) {
  GELOGD("[%s] Start to execute.", context.GetNodeName());
  GE_CHK_STATUS_RET(DoCopyTensor(context, 0));

  if (done_callback) {
    GE_CHK_STATUS_RET(context.RegisterCallback(done_callback));
  }

  GELOGD("[%s] Done executing successfully.", context.GetNodeName());
  return SUCCESS;
}

Status IdentityNodeTask::UpdateArgs(TaskContext &context) { return SUCCESS; }

Status IdentityNNodeTask::ExecuteAsync(TaskContext &context, std::function<void()> done_callback) {
  GELOGD("[%s] Start to execute.", context.GetNodeName());
  for (int i = 0; i < context.NumInputs(); ++i) {
    GE_CHK_STATUS_RET(DoCopyTensor(context, i));
  }

  if (done_callback) {
    GE_CHK_STATUS_RET(context.RegisterCallback(done_callback));
  }

  GELOGD("[%s] Done executing successfully.", context.GetNodeName());
  return SUCCESS;
}

Status RtsNodeExecutor::LoadTask(const HybridModel &model, const NodePtr &node, shared_ptr<NodeTask> &task) const {
  auto op_type = node->GetType();
  if (op_type == IDENTITY) {
    task = MakeShared<IdentityNodeTask>();
  } else if (op_type == IDENTITYN) {
    task = MakeShared<IdentityNNodeTask>();
  } else {
    GELOGE(INTERNAL_ERROR, "[%s] Unsupported RTS op type: %s", node->GetName().c_str(), op_type.c_str());
    return INTERNAL_ERROR;
  }

  GE_CHECK_NOTNULL(task);
  return SUCCESS;
}
}  // namespace hybrid
}  // namespace ge