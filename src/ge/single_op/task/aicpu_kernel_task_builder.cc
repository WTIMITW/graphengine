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

#include "single_op/task/aicpu_kernel_task_builder.h"

namespace ge {
AiCpuCCTaskBuilder::AiCpuCCTaskBuilder(const OpDescPtr &op_desc, const domi::KernelDef &kernel_def)
    : op_desc_(op_desc), kernel_def_(kernel_def) {}

Status AiCpuCCTaskBuilder::SetKernelArgs(AiCpuCCTask &task) {
  size_t aicpu_arg_size = kernel_def_.args_size();
  if (aicpu_arg_size <= 0) {
    GELOGE(RT_FAILED, "aicpu_arg_size is invalid, value = %zu", aicpu_arg_size);
    return RT_FAILED;
  }
  std::unique_ptr<uint8_t[]> aicpu_args;
  aicpu_args.reset(new (std::nothrow) uint8_t[aicpu_arg_size]());
  if (aicpu_args == nullptr) {
    GELOGE(RT_FAILED, "malloc failed, size = %zu", aicpu_arg_size);
    return RT_FAILED;
  }

  auto err = memcpy_s(aicpu_args.get(), aicpu_arg_size, kernel_def_.args().data(), aicpu_arg_size);
  if (err != EOK) {
    GELOGE(RT_FAILED, "memcpy_s args failed, size = %zu, err = %d", aicpu_arg_size, err);
    return RT_FAILED;
  }

  task.SetIoAddr(aicpu_args.get() + sizeof(aicpu::AicpuParamHead));
  task.SetKernelArgs(std::move(aicpu_args), aicpu_arg_size);
  return SUCCESS;
}

Status AiCpuCCTaskBuilder::BuildTask(AiCpuCCTask &task) {
  auto ret = SetKernelArgs(task);
  if (ret != SUCCESS) {
    return ret;
  }
  const std::string &so_name = kernel_def_.so_name();
  const std::string &kernel_name = kernel_def_.kernel_name();
  task.SetSoName(so_name);
  task.SetkernelName(kernel_name);
  task.op_desc_ = op_desc_;
  return SUCCESS;
}
}  // namespace ge