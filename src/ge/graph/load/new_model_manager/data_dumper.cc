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

#include "graph/load/new_model_manager/data_dumper.h"

#include <sys/time.h>
#include <cstdlib>
#include <ctime>
#include <map>
#include <utility>
#include <vector>

#include "common/debug/memory_dumper.h"
#include "common/properties_manager.h"
#include "common/util.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/util.h"
#include "graph/anchor.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/load/new_model_manager/model_utils.h"
#include "graph/manager/util/debug.h"
#include "graph/utils/attr_utils.h"
#include "graph/utils/tensor_utils.h"
#include "proto/dump_task.pb.h"
#include "proto/ge_ir.pb.h"
#include "proto/op_mapping_info.pb.h"
#include "runtime/base.h"
#include "runtime/mem.h"

namespace {
const uint32_t kAicpuLoadFlag = 1;
const uint32_t kAicpuUnloadFlag = 0;
const int64_t kOpDebugSize = 2048;
const int64_t kOpDebugShape = 2048;
const int8_t kDecimal = 10;
const uint32_t kAddrLen = sizeof(void *);
const char *const kDumpOutput = "output";
const char *const kDumpInput = "input";
const char *const kDumpAll = "all";

// parse for format like nodename:input:index
static bool ParseNameIndex(const std::string &node_name_index, std::string &node_name, std::string &input_or_output,
                           size_t &index) {
  auto sep = node_name_index.rfind(':');
  if (sep == std::string::npos) {
    return false;
  }
  auto index_str = node_name_index.substr(sep + 1);
  index = static_cast<size_t>(std::strtol(index_str.c_str(), nullptr, kDecimal));
  auto node_name_without_index = node_name_index.substr(0, sep);
  sep = node_name_without_index.rfind(':');
  if (sep == std::string::npos) {
    return false;
  }
  node_name = node_name_without_index.substr(0, sep);
  input_or_output = node_name_without_index.substr(sep + 1);
  return !(input_or_output != kDumpInput && input_or_output != kDumpOutput);
}

static bool IsTensorDescWithSkipDumpAddrType(bool has_mem_type_attr, vector<int64_t> v_memory_type, size_t i) {
  return has_mem_type_attr && (v_memory_type[i] == RT_MEMORY_L1);
}

static uint64_t GetNowTime() {
  uint64_t ret = 0;
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    ret = tv.tv_sec * 1000000ULL + tv.tv_usec;
  }

  return ret;
}
}  // namespace

static int32_t GetIrDataType(ge::DataType data_type) {
  static const std::map<ge::DataType, ge::proto::DataType> data_type_map = {
    {ge::DT_UNDEFINED, ge::proto::DT_UNDEFINED},
    {ge::DT_FLOAT, ge::proto::DT_FLOAT},
    {ge::DT_FLOAT16, ge::proto::DT_FLOAT16},
    {ge::DT_INT8, ge::proto::DT_INT8},
    {ge::DT_UINT8, ge::proto::DT_UINT8},
    {ge::DT_INT16, ge::proto::DT_INT16},
    {ge::DT_UINT16, ge::proto::DT_UINT16},
    {ge::DT_INT32, ge::proto::DT_INT32},
    {ge::DT_INT64, ge::proto::DT_INT64},
    {ge::DT_UINT32, ge::proto::DT_UINT32},
    {ge::DT_UINT64, ge::proto::DT_UINT64},
    {ge::DT_BOOL, ge::proto::DT_BOOL},
    {ge::DT_DOUBLE, ge::proto::DT_DOUBLE},
    {ge::DT_DUAL, ge::proto::DT_DUAL},
    {ge::DT_DUAL_SUB_INT8, ge::proto::DT_DUAL_SUB_INT8},
    {ge::DT_DUAL_SUB_UINT8, ge::proto::DT_DUAL_SUB_UINT8},
    {ge::DT_COMPLEX64, ge::proto::DT_COMPLEX64},
    {ge::DT_COMPLEX128, ge::proto::DT_COMPLEX128},
    {ge::DT_QINT8, ge::proto::DT_QINT8},
    {ge::DT_QINT16, ge::proto::DT_QINT16},
    {ge::DT_QINT32, ge::proto::DT_QINT32},
    {ge::DT_QUINT8, ge::proto::DT_QUINT8},
    {ge::DT_QUINT16, ge::proto::DT_QUINT16},
    {ge::DT_RESOURCE, ge::proto::DT_RESOURCE},
    {ge::DT_STRING_REF, ge::proto::DT_STRING_REF},
    {ge::DT_STRING, ge::proto::DT_STRING},
  };

  auto iter = data_type_map.find(data_type);
  if (iter == data_type_map.end()) {
    return static_cast<int32_t>(ge::proto::DT_UNDEFINED);
  }

  return static_cast<int32_t>(iter->second);
}

namespace ge {
DataDumper::~DataDumper() {
  ReleaseDevMem(&dev_mem_load_);
  ReleaseDevMem(&dev_mem_unload_);
}

void DataDumper::ReleaseDevMem(void **ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }

  if (*ptr != nullptr) {
    rtError_t rt_ret = rtFree(*ptr);
    if (rt_ret != RT_ERROR_NONE) {
      GELOGE(RT_FAILED, "Call rtFree failed, ret: 0x%X", rt_ret);
    }

    *ptr = nullptr;
  }
}

void DataDumper::SetLoopAddr(void *global_step, void *loop_per_iter, void *loop_cond) {
  global_step_ = reinterpret_cast<uintptr_t>(global_step);
  loop_per_iter_ = reinterpret_cast<uintptr_t>(loop_per_iter);
  loop_cond_ = reinterpret_cast<uintptr_t>(loop_cond);
}

void DataDumper::SaveDumpInput(const std::shared_ptr<Node> &node) {
  GELOGI("Start to save data %s message", node->GetName().c_str());
  if (node != nullptr) {
    auto input_op_desc = node->GetOpDesc();
    if (input_op_desc == nullptr) {
      GELOGE(PARAM_INVALID, "input op desc is null.");
      return;
    }

    for (auto &out_data_anchor : node->GetAllOutDataAnchors()) {
      for (auto &dst_in_data_anchor : out_data_anchor->GetPeerInDataAnchors()) {
        ge::NodePtr dst_node = dst_in_data_anchor->GetOwnerNode();
        auto op_desc = dst_node->GetOpDesc();
        if (op_desc == nullptr) {
          GELOGE(PARAM_INVALID, "input op desc is null.");
          return;
        }

        input_map_.insert(
          {op_desc->GetName(), {input_op_desc, dst_in_data_anchor->GetIdx(), out_data_anchor->GetIdx()}});
      }
    }
    GELOGI("Save data message successfully");
  }
}

void DataDumper::SaveEndGraphId(uint32_t task_id, uint32_t stream_id) {
  end_graph_task_id_ = task_id;
  end_graph_stream_id_ = stream_id;
}

void DataDumper::SaveOpDebugId(uint32_t task_id, uint32_t stream_id, void *op_debug_addr, bool is_op_debug) {
  op_debug_task_id_ = task_id;
  op_debug_stream_id_ = stream_id;
  op_debug_addr_ = op_debug_addr;
  is_op_debug_ = is_op_debug;
}

void DataDumper::SaveDumpOpInfo(const RuntimeParam &model_param, const OpDescPtr &op, uint32_t task_id,
                                uint32_t stream_id) {
  GELOGD("Start SaveDumpOpInfo of task_id: %u, stream_id: %u", task_id, stream_id);
  OpDescInfo op_desc_info;
  op_desc_info.op_name = op->GetName();
  op_desc_info.op_type = op->GetType();
  op_desc_info.task_id = task_id;
  op_desc_info.stream_id = stream_id;
  for (size_t i = 0; i < op->GetInputsSize(); ++i) {
    GeTensorDesc input_desc = op->GetInputDesc(i);
    op_desc_info.input_format.emplace_back(input_desc.GetFormat());
    op_desc_info.input_shape.emplace_back(input_desc.GetShape().GetDims());
    op_desc_info.input_data_type.emplace_back(input_desc.GetDataType());
    int64_t input_size = 0;
    auto tensor_descs = op->GetAllInputsDesc();
    if (TensorUtils::GetTensorSizeInBytes(tensor_descs.at(i), input_size) != SUCCESS) {
      GELOGW("Get input size failed");
      return;
    }
    GELOGI("Save dump op info, the input size is %ld", input_size);
    op_desc_info.input_size.emplace_back(input_size);
  }
  for (size_t j = 0; j < op->GetOutputsSize(); ++j) {
    GeTensorDesc output_desc = op->GetOutputDesc(j);
    op_desc_info.output_format.emplace_back(output_desc.GetFormat());
    op_desc_info.output_shape.emplace_back(output_desc.GetShape().GetDims());
    op_desc_info.output_data_type.emplace_back(output_desc.GetDataType());
    int64_t output_size = 0;
    auto tensor_descs = op->GetAllOutputsDesc();
    if (TensorUtils::GetTensorSizeInBytes(tensor_descs.at(j), output_size) != SUCCESS) {
      GELOGW("Get input size failed");
      return;
    }
    GELOGI("Save dump op info, the output size is %ld", output_size);
    op_desc_info.output_size.emplace_back(output_size);
  }
  op_desc_info.input_addrs = ModelUtils::GetInputDataAddrs(model_param, op);
  op_desc_info.output_addrs = ModelUtils::GetOutputDataAddrs(model_param, op);

  op_desc_info_.emplace_back(op_desc_info);
}

bool DataDumper::GetOpDescInfo(uint32_t stream_id, uint32_t task_id, OpDescInfo &op_desc_info) const {
  GELOGI("There are %zu op need to dump.", op_desc_info_.size());
  for (size_t index = 0; index < op_desc_info_.size(); ++index) {
    OpDescInfo dump_op_info = op_desc_info_.at(index);
    if (dump_op_info.task_id == task_id && dump_op_info.stream_id == stream_id) {
      GELOGI("find exception op of task_id: %u, stream_id: %u.", task_id, stream_id);
      op_desc_info = dump_op_info;
      return true;
    }
  }
  return false;
}

void DataDumper::SaveDumpTask(uint32_t task_id, uint32_t stream_id, const std::shared_ptr<OpDesc> &op_desc,
                              uintptr_t args) {
  if (op_desc == nullptr) {
    GELOGE(PARAM_INVALID, "Opdesc is nullptr");
    return;
  }

  GELOGI("Save dump task %s, task id: %u, stream id: %u", op_desc->GetName().c_str(), task_id, stream_id);
  op_list_.push_back({task_id, stream_id, op_desc, args, true});

  for (auto iter = input_map_.equal_range(op_desc->GetName()); iter.first != iter.second; ++iter.first) {
    InnerInputMapping &inner_input_mapping = iter.first->second;
    auto &data_op = inner_input_mapping.data_op;
    if (data_op == nullptr) {
      GELOGE(PARAM_INVALID, "data_op is null.");
      return;
    }

    auto input_tensor = op_desc->GetInputDescPtr(inner_input_mapping.input_anchor_index);
    if (input_tensor == nullptr) {
      GELOGE(PARAM_INVALID, "input_tensor is null, index: %d, size: %zu.", inner_input_mapping.input_anchor_index,
             op_desc->GetInputsSize());
      return;
    }

    int64_t data_size = 0;
    if (AttrUtils::GetInt(input_tensor, ATTR_NAME_INPUT_ORIGIN_SIZE, data_size)) {
      GELOGI("Get aipp data size according to attr is %ld", data_size);
    } else if (TensorUtils::GetTensorSizeInBytes(*input_tensor, data_size) != SUCCESS) {
      GELOGE(PARAM_INVALID, "Get input size filed");
      return;
    }

    GELOGI("Save input dump task %s, id: %u,stream id :%u,data size :%ld", data_op->GetName().c_str(), task_id,
           stream_id, data_size);
    op_list_.push_back({task_id, stream_id, data_op, args, false, inner_input_mapping.input_anchor_index,
                        inner_input_mapping.output_anchor_index, input_tensor->GetShape().GetDims(), data_size});
  }
}

static void SetOpMappingLoopAddr(uintptr_t step_id, uintptr_t loop_per_iter, uintptr_t loop_cond,
                                 aicpu::dump::OpMappingInfo &op_mapping_info) {
  if (step_id != 0) {
    GELOGI("step_id exists.");
    op_mapping_info.set_step_id_addr(static_cast<uint64_t>(step_id));
  } else {
    GELOGI("step_id is null.");
  }

  if (loop_per_iter != 0) {
    GELOGI("loop_per_iter exists.");
    op_mapping_info.set_iterations_per_loop_addr(static_cast<uint64_t>(loop_per_iter));
  } else {
    GELOGI("loop_per_iter is null.");
  }

  if (loop_cond != 0) {
    GELOGI("loop_cond exists.");
    op_mapping_info.set_loop_cond_addr(static_cast<uint64_t>(loop_cond));
  } else {
    GELOGI("loop_cond is null.");
  }
}

Status DataDumper::GenerateOutput(aicpu::dump::Output &output, const OpDesc::Vistor<GeTensorDesc> &tensor_descs,
                                  const uintptr_t &addr, size_t index) {
  output.set_data_type(static_cast<int32_t>(GetIrDataType(tensor_descs.at(index).GetDataType())));
  output.set_format(static_cast<int32_t>(tensor_descs.at(index).GetFormat()));

  for (auto dim : tensor_descs.at(index).GetShape().GetDims()) {
    output.mutable_shape()->add_dim(dim);
  }
  int64_t output_size = 0;
  if (TensorUtils::GetTensorSizeInBytes(tensor_descs.at(index), output_size) != SUCCESS) {
    GELOGE(PARAM_INVALID, "Get output size filed");
    return PARAM_INVALID;
  }
  GELOGD("Get output size in dump is %ld", output_size);
  std::string origin_name;
  int32_t origin_output_index = -1;
  (void)AttrUtils::GetStr(&tensor_descs.at(index), ATTR_NAME_DATA_DUMP_ORIGIN_NAME, origin_name);
  (void)AttrUtils::GetInt(&tensor_descs.at(index), ATTR_NAME_DATA_DUMP_ORIGIN_OUTPUT_INDEX, origin_output_index);
  output.set_size(output_size);
  output.set_original_name(origin_name);
  output.set_original_output_index(origin_output_index);
  output.set_original_output_format(static_cast<int32_t>(tensor_descs.at(index).GetOriginFormat()));
  output.set_original_output_data_type(static_cast<int32_t>(tensor_descs.at(index).GetOriginDataType()));
  output.set_address(static_cast<uint64_t>(addr));
  return SUCCESS;
}

Status DataDumper::DumpRefOutput(const DataDumper::InnerDumpInfo &inner_dump_info, aicpu::dump::Output &output,
                                 size_t i, const std::string &node_name_index) {
  std::string dump_op_name;
  std::string input_or_output;
  size_t index;
  // parser and find which node's input or output tensor desc is chosen for dump info
  if (!ParseNameIndex(node_name_index, dump_op_name, input_or_output, index)) {
    GELOGE(PARAM_INVALID, "Op [%s] output desc[%zu] with invalid ATTR_DATA_DUMP_REF attr[%s].",
           inner_dump_info.op->GetName().c_str(), i, node_name_index.c_str());
    return PARAM_INVALID;
  }
  GE_CHECK_NOTNULL(compute_graph_);
  auto replace_node = compute_graph_->FindNode(dump_op_name);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(replace_node == nullptr,
                                       "Op [%s] output desc[%zu] with invalid ATTR_DATA_DUMP_REF attr[%s],"
                                       " cannot find redirect node[%s].",
                                       inner_dump_info.op->GetName().c_str(), i, node_name_index.c_str(),
                                       dump_op_name.c_str());
  auto replace_opdesc = replace_node->GetOpDesc();
  GE_CHECK_NOTNULL(replace_opdesc);
  auto iter = ref_info_.find(replace_opdesc);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(iter == ref_info_.end(),
                                       "Op [%s] output desc[%zu] cannot find any saved redirect node[%s]'s info.",
                                       inner_dump_info.op->GetName().c_str(), i, replace_opdesc->GetName().c_str());
  GE_CHECK_NOTNULL(iter->second);
  auto addr = reinterpret_cast<uintptr_t>(iter->second);
  if (input_or_output == kDumpInput) {
    const auto &replace_input_descs = replace_opdesc->GetAllInputsDesc();
    addr += kAddrLen * index;
    GE_CHK_STATUS_RET(GenerateOutput(output, replace_input_descs, addr, index), "Generate output failed");
  } else if (input_or_output == kDumpOutput) {
    const auto &replace_output_descs = replace_opdesc->GetAllOutputsDesc();
    const auto replace_input_size = replace_opdesc->GetAllInputsDesc().size();
    addr += (index + replace_input_size) * kAddrLen;
    GE_CHK_STATUS_RET(GenerateOutput(output, replace_output_descs, addr, index), "Generate output failed");
  }
  GELOGD("Op [%s] output desc[%zu] dump info is replaced by node[%s] [%s] tensor_desc [%zu]",
         inner_dump_info.op->GetName().c_str(), i, dump_op_name.c_str(), input_or_output.c_str(), index);
  return SUCCESS;
}

Status DataDumper::DumpOutputWithTask(const InnerDumpInfo &inner_dump_info, aicpu::dump::Task &task) {
  const auto &output_descs = inner_dump_info.op->GetAllOutputsDesc();
  const std::vector<void *> output_addrs = ModelUtils::GetOutputDataAddrs(runtime_param_, inner_dump_info.op);
  if (output_descs.size() != output_addrs.size()) {
    GELOGE(PARAM_INVALID, "Invalid output desc addrs size %zu, op %s has %zu output desc.", output_addrs.size(),
           inner_dump_info.op->GetName().c_str(), output_descs.size());
    return PARAM_INVALID;
  }
  std::vector<int64_t> v_memory_type;
  bool has_mem_type_attr = ge::AttrUtils::GetListInt(inner_dump_info.op, ATTR_NAME_OUTPUT_MEM_TYPE_LIST, v_memory_type);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(has_mem_type_attr && (v_memory_type.size() != output_descs.size()),
                                       "DumpOutputWithTask[%s], output size[%zu], output memory type size[%zu]",
                                       inner_dump_info.op->GetName().c_str(), output_descs.size(),
                                       v_memory_type.size());

  for (size_t i = 0; i < output_descs.size(); ++i) {
    aicpu::dump::Output output;
    std::string node_name_index;
    const auto &output_desc = output_descs.at(i);
    // check dump output tensor desc is redirected by attr ATTR_DATA_DUMP_REF
    if (AttrUtils::GetStr(&output_desc, ATTR_DATA_DUMP_REF, node_name_index)) {
      GE_CHK_STATUS_RET(DumpRefOutput(inner_dump_info, output, i, node_name_index), "DumpRefOutput failed");
      task.mutable_output()->Add(std::move(output));
    } else {
      if (IsTensorDescWithSkipDumpAddrType(has_mem_type_attr, v_memory_type, i)) {
        GELOGI("[L1Fusion] DumpOutputWithTask[%s] output[%zu] is l1 addr.", inner_dump_info.op->GetName().c_str(), i);
        int64_t output_size = 0;
        if (TensorUtils::GetTensorSizeInBytes(output_descs.at(i), output_size) != SUCCESS) {
          GELOGE(PARAM_INVALID, "Get output size failed.");
          return PARAM_INVALID;
        }
        GELOGI("Get output size of l1_fusion_dump is %ld", output_size);
        GenerateOpBuffer(output_size, task);
      } else {
        const auto input_size = inner_dump_info.op->GetInputsSize();
        auto addr = inner_dump_info.args + (i + input_size) * kAddrLen;
        GE_CHK_STATUS_RET(GenerateOutput(output, output_descs, addr, i), "Generate output failed");
        task.mutable_output()->Add(std::move(output));
      }
    }
  }
  return SUCCESS;
}

Status DataDumper::DumpOutput(const InnerDumpInfo &inner_dump_info, aicpu::dump::Task &task) {
  GELOGI("Start dump output");
  if (inner_dump_info.is_task) {
    // tbe or aicpu op, these ops are with task
    return DumpOutputWithTask(inner_dump_info, task);
  }
  // else data, const or variable op
  aicpu::dump::Output output;
  auto output_tensor = inner_dump_info.op->GetOutputDescPtr(inner_dump_info.output_anchor_index);
  const std::vector<void *> output_addrs = ModelUtils::GetOutputDataAddrs(runtime_param_, inner_dump_info.op);
  if (output_tensor == nullptr) {
    GELOGE(PARAM_INVALID, "output_tensor is null, index: %d, size: %zu.", inner_dump_info.output_anchor_index,
           inner_dump_info.op->GetOutputsSize());
    return PARAM_INVALID;
  }

  output.set_data_type(static_cast<int32_t>(GetIrDataType(output_tensor->GetDataType())));
  output.set_format(static_cast<int32_t>(output_tensor->GetFormat()));

  for (auto dim : inner_dump_info.dims) {
    output.mutable_shape()->add_dim(dim);
  }

  std::string origin_name;
  int32_t origin_output_index = -1;
  (void)AttrUtils::GetStr(output_tensor, ATTR_NAME_DATA_DUMP_ORIGIN_NAME, origin_name);
  (void)AttrUtils::GetInt(output_tensor, ATTR_NAME_DATA_DUMP_ORIGIN_OUTPUT_INDEX, origin_output_index);
  output.set_size(inner_dump_info.data_size);
  output.set_original_name(origin_name);
  output.set_original_output_index(origin_output_index);
  output.set_original_output_format(static_cast<int32_t>(output_tensor->GetOriginFormat()));
  output.set_original_output_data_type(static_cast<int32_t>(output_tensor->GetOriginDataType()));
  // due to lhisi virtual addr bug, cannot use args now
  if (inner_dump_info.output_anchor_index >= static_cast<int>(output_addrs.size())) {
    GELOGE(FAILED, "Index is out of range.");
    return FAILED;
  }
  auto data_addr = inner_dump_info.args + kAddrLen * static_cast<uint32_t>(inner_dump_info.input_anchor_index);
  output.set_address(static_cast<uint64_t>(data_addr));

  task.mutable_output()->Add(std::move(output));

  return SUCCESS;
}

Status DataDumper::GenerateInput(aicpu::dump::Input &input, const OpDesc::Vistor<GeTensorDesc> &tensor_descs,
                                 const uintptr_t &addr, size_t index) {
  input.set_data_type(static_cast<int32_t>(GetIrDataType(tensor_descs.at(index).GetDataType())));
  input.set_format(static_cast<int32_t>(tensor_descs.at(index).GetFormat()));

  for (auto dim : tensor_descs.at(index).GetShape().GetDims()) {
    input.mutable_shape()->add_dim(dim);
  }
  int64_t input_size = 0;
  if (AttrUtils::GetInt(tensor_descs.at(index), ATTR_NAME_INPUT_ORIGIN_SIZE, input_size)) {
    GELOGI("Get aipp input size according to attr is %ld", input_size);
  } else if (TensorUtils::GetTensorSizeInBytes(tensor_descs.at(index), input_size) != SUCCESS) {
    GELOGE(PARAM_INVALID, "Get input size filed");
    return PARAM_INVALID;
  }
  GELOGD("Get input size in dump is %ld", input_size);
  input.set_size(input_size);
  input.set_address(static_cast<uint64_t>(addr));
  return SUCCESS;
}

Status DataDumper::DumpRefInput(const DataDumper::InnerDumpInfo &inner_dump_info, aicpu::dump::Input &input, size_t i,
                                const std::string &node_name_index) {
  std::string dump_op_name;
  std::string input_or_output;
  size_t index;
  // parser and find which node's input or output tensor desc is chosen for dump info
  if (!ParseNameIndex(node_name_index, dump_op_name, input_or_output, index)) {
    GELOGE(PARAM_INVALID, "Op [%s] input desc[%zu] with invalid ATTR_DATA_DUMP_REF attr[%s].",
           inner_dump_info.op->GetName().c_str(), i, node_name_index.c_str());
    return PARAM_INVALID;
  }
  GE_CHECK_NOTNULL(compute_graph_);
  auto replace_node = compute_graph_->FindNode(dump_op_name);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(replace_node == nullptr,
                                       "Op [%s] input desc[%zu] with invalid ATTR_DATA_DUMP_REF attr[%s],"
                                       " cannot find redirect node[%s].",
                                       inner_dump_info.op->GetName().c_str(), i, node_name_index.c_str(),
                                       dump_op_name.c_str());
  auto replace_opdesc = replace_node->GetOpDesc();
  GE_CHECK_NOTNULL(replace_opdesc);
  auto iter = ref_info_.find(replace_opdesc);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(iter == ref_info_.end(),
                                       "Op [%s] input desc[%zu] cannot find any saved redirect node[%s]'s info.",
                                       inner_dump_info.op->GetName().c_str(), i, replace_opdesc->GetName().c_str());
  GE_CHECK_NOTNULL(iter->second);
  auto addr = reinterpret_cast<uintptr_t>(iter->second);
  if (input_or_output == kDumpInput) {
    const auto &replace_input_descs = replace_opdesc->GetAllInputsDesc();
    addr += kAddrLen * index;
    GE_CHK_STATUS_RET(GenerateInput(input, replace_input_descs, addr, index), "Generate input failed");
  } else if (input_or_output == kDumpOutput) {
    const auto &replace_output_descs = replace_opdesc->GetAllOutputsDesc();
    const auto replace_input_size = replace_opdesc->GetAllInputsDesc().size();
    addr += (index + replace_input_size) * kAddrLen;
    GE_CHK_STATUS_RET(GenerateInput(input, replace_output_descs, addr, index), "Generate input failed");
  }
  GELOGD("Op [%s] input desc[%zu] dump info is replaced by node[%s] [%s] tensor_desc [%zu]",
         inner_dump_info.op->GetName().c_str(), i, dump_op_name.c_str(), input_or_output.c_str(), index);
  return SUCCESS;
}

Status DataDumper::DumpInput(const InnerDumpInfo &inner_dump_info, aicpu::dump::Task &task) {
  GELOGI("Start dump input");
  const auto &input_descs = inner_dump_info.op->GetAllInputsDesc();
  const std::vector<void *> input_addrs = ModelUtils::GetInputDataAddrs(runtime_param_, inner_dump_info.op);
  if (input_descs.size() != input_addrs.size()) {
    GELOGE(PARAM_INVALID, "Invalid input desc addrs size %zu, op %s has %zu input desc.", input_addrs.size(),
           inner_dump_info.op->GetName().c_str(), input_descs.size());
    return PARAM_INVALID;
  }
  std::vector<int64_t> v_memory_type;
  bool has_mem_type_attr = ge::AttrUtils::GetListInt(inner_dump_info.op, ATTR_NAME_INPUT_MEM_TYPE_LIST, v_memory_type);
  GE_RT_PARAM_INVALID_WITH_LOG_IF_TRUE(has_mem_type_attr && (v_memory_type.size() != input_descs.size()),
                                       "DumpInput[%s], input size[%zu], input memory type size[%zu]",
                                       inner_dump_info.op->GetName().c_str(), input_descs.size(), v_memory_type.size());

  for (size_t i = 0; i < input_descs.size(); ++i) {
    aicpu::dump::Input input;
    std::string node_name_index;
    // check dump input tensor desc is redirected by attr ATTR_DATA_DUMP_REF
    if (AttrUtils::GetStr(&input_descs.at(i), ATTR_DATA_DUMP_REF, node_name_index)) {
      GE_CHK_STATUS_RET(DumpRefInput(inner_dump_info, input, i, node_name_index), "DumpRefInput failed");
      task.mutable_input()->Add(std::move(input));
      // normal dump without attr
    } else {
      if (IsTensorDescWithSkipDumpAddrType(has_mem_type_attr, v_memory_type, i)) {
        GELOGI("[L1Fusion] DumpInput[%s] input[%zu] is l1 addr", inner_dump_info.op->GetName().c_str(), i);
        int64_t input_size = 0;
        if (AttrUtils::GetInt(input_descs.at(i), ATTR_NAME_INPUT_ORIGIN_SIZE, input_size)) {
          GELOGI("Get aipp input size according to attr is %ld", input_size);
        } else if (TensorUtils::GetTensorSizeInBytes(input_descs.at(i), input_size) != SUCCESS) {
          GELOGE(PARAM_INVALID, "Get input size failed.");
          return PARAM_INVALID;
        }
        GELOGI("Get input size of l1_fusion_dump is %ld", input_size);
        GenerateOpBuffer(input_size, task);
      } else {
        auto addr = inner_dump_info.args + kAddrLen * i;
        GE_CHK_STATUS_RET(GenerateInput(input, input_descs, addr, i), "Generate input failed");
        task.mutable_input()->Add(std::move(input));
      }
    }
  }
  return SUCCESS;
}

void DataDumper::GenerateOpBuffer(const int64_t &size, aicpu::dump::Task &task) {
  aicpu::dump::OpBuffer op_buffer;
  op_buffer.set_buffer_type(aicpu::dump::BufferType::L1);
  op_buffer.set_address(reinterpret_cast<uintptr_t>(l1_fusion_addr_));
  op_buffer.set_size(size);
  task.mutable_buffer()->Add(std::move(op_buffer));
}

Status DataDumper::ExecuteLoadDumpInfo(aicpu::dump::OpMappingInfo &op_mapping_info) {
  std::string proto_str;
  size_t proto_size = op_mapping_info.ByteSizeLong();
  bool ret = op_mapping_info.SerializeToString(&proto_str);
  if (!ret || proto_size == 0) {
    GELOGE(PARAM_INVALID, "Protobuf SerializeToString failed, proto size %zu.", proto_size);
    return PARAM_INVALID;
  }

  if (dev_mem_load_ != nullptr) {
    GELOGW("dev_mem_load_ has been used.");
    ReleaseDevMem(&dev_mem_load_);
  }

  rtError_t rt_ret = rtMalloc(&dev_mem_load_, proto_size, RT_MEMORY_HBM);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtMalloc failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }
  GE_PRINT_DYNAMIC_MEMORY(rtMalloc, "load dump information.", proto_size)

  rt_ret = rtMemcpy(dev_mem_load_, proto_size, proto_str.c_str(), proto_size, RT_MEMCPY_HOST_TO_DEVICE);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtMemcpy failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }

  rt_ret = rtDatadumpInfoLoad(dev_mem_load_, proto_size);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtDatadumpInfoLoad failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }

  load_flag_ = true;
  GELOGI("LoadDumpInfo success, proto size is: %zu.", proto_size);
  return SUCCESS;
}

Status DataDumper::ExecuteUnLoadDumpInfo(aicpu::dump::OpMappingInfo &op_mapping_info) {
  std::string proto_str;
  size_t proto_size = op_mapping_info.ByteSizeLong();
  bool ret = op_mapping_info.SerializeToString(&proto_str);
  if (!ret || proto_size == 0) {
    GELOGE(PARAM_INVALID, "Protobuf SerializeToString failed, proto size %zu.", proto_size);
    return PARAM_INVALID;
  }

  if (dev_mem_unload_ != nullptr) {
    GELOGW("dev_mem_unload_ has been used.");
    ReleaseDevMem(&dev_mem_unload_);
  }

  rtError_t rt_ret = rtMalloc(&dev_mem_unload_, proto_size, RT_MEMORY_HBM);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtMalloc failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }
  GE_PRINT_DYNAMIC_MEMORY(rtMalloc, "unload dump information.", proto_size)

  rt_ret = rtMemcpy(dev_mem_unload_, proto_size, proto_str.c_str(), proto_size, RT_MEMCPY_HOST_TO_DEVICE);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtMemcpy failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }

  rt_ret = rtDatadumpInfoLoad(dev_mem_unload_, proto_size);
  if (rt_ret != RT_ERROR_NONE) {
    GELOGE(RT_FAILED, "Call rtDatadumpInfoLoad failed, ret: 0x%X", rt_ret);
    return RT_ERROR_TO_GE_STATUS(rt_ret);
  }
  load_flag_ = false;
  GELOGI("UnloadDumpInfo success, proto size is: %zu.", proto_size);
  return SUCCESS;
}

Status DataDumper::LoadDumpInfo() {
  std::string dump_list_key;
  PrintCheckLog(dump_list_key);

  if (op_list_.empty()) {
    GELOGW("op_list_ is empty");
  }

  aicpu::dump::OpMappingInfo op_mapping_info;

  auto dump_path = dump_properties_.GetDumpPath() + std::to_string(device_id_) + "/";
  op_mapping_info.set_dump_path(dump_path);
  op_mapping_info.set_model_name(dump_list_key);
  op_mapping_info.set_model_id(model_id_);
  op_mapping_info.set_flag(kAicpuLoadFlag);
  op_mapping_info.set_dump_step(dump_properties_.GetDumpStep());
  SetOpMappingLoopAddr(global_step_, loop_per_iter_, loop_cond_, op_mapping_info);
  GELOGI("Dump step is %s and dump path is %s dump model is %s in load dump info",
         dump_properties_.GetDumpStep().c_str(), dump_path.c_str(), dump_list_key.c_str());
  auto ret = BuildTaskInfo(op_mapping_info);
  if (ret != SUCCESS) {
    GELOGE(ret, "Build task info failed");
    return ret;
  }

  SetEndGraphIdToAicpu(end_graph_task_id_, end_graph_stream_id_, op_mapping_info);

  SetOpDebugIdToAicpu(op_debug_task_id_, op_debug_stream_id_, op_debug_addr_, op_mapping_info);

  if (!op_list_.empty() || is_op_debug_ || is_end_graph_) {
    auto ret = ExecuteLoadDumpInfo(op_mapping_info);
    if (ret != SUCCESS) {
      GELOGE(ret, "Execute load dump info failed");
      return ret;
    }
  }
  return SUCCESS;
}

Status DataDumper::BuildTaskInfo(aicpu::dump::OpMappingInfo &op_mapping_info) {
  for (const auto &op_iter : op_list_) {
    auto op_desc = op_iter.op;
    GELOGD("Op %s in model begin to add task in op_mapping_info", op_desc->GetName().c_str());
    aicpu::dump::Task task;
    task.set_end_graph(false);
    task.set_task_id(op_iter.task_id);
    task.set_stream_id(op_iter.stream_id);
    task.mutable_op()->set_op_name(op_desc->GetName());
    task.mutable_op()->set_op_type(op_desc->GetType());

    if (dump_properties_.GetDumpMode() == kDumpOutput) {
      Status ret = DumpOutput(op_iter, task);
      if (ret != SUCCESS) {
        GELOGE(ret, "Dump output failed");
        return ret;
      }
      op_mapping_info.mutable_task()->Add(std::move(task));
      continue;
    }
    if (dump_properties_.GetDumpMode() == kDumpInput) {
      if (op_iter.is_task) {
        GE_CHK_STATUS_RET(DumpInput(op_iter, task), "Dump input failed");
      }
      op_mapping_info.mutable_task()->Add(std::move(task));
      continue;
    }
    if (dump_properties_.GetDumpMode() == kDumpAll || is_op_debug_) {
      auto ret = DumpOutput(op_iter, task);
      if (ret != SUCCESS) {
        GELOGE(ret, "Dump output failed when in dumping all");
        return ret;
      }
      if (op_iter.is_task) {
        ret = DumpInput(op_iter, task);
        if (ret != SUCCESS) {
          GELOGE(ret, "Dump input failed when in dumping all");
          return ret;
        }
      }
      op_mapping_info.mutable_task()->Add(std::move(task));
      continue;
    }
  }
  return SUCCESS;
}

void DataDumper::SetEndGraphIdToAicpu(uint32_t task_id, uint32_t stream_id,
                                      aicpu::dump::OpMappingInfo &op_mapping_info) {
  if (dump_properties_.GetDumpMode() == kDumpOutput || dump_properties_.GetDumpMode() == kDumpInput ||
      dump_properties_.GetDumpMode() == kDumpAll) {
    aicpu::dump::Task task;
    task.set_end_graph(true);
    task.set_task_id(end_graph_task_id_);
    task.set_stream_id(end_graph_stream_id_);
    task.mutable_op()->set_op_name(NODE_NAME_END_GRAPH);
    task.mutable_op()->set_op_type(ENDGRAPH);
    op_mapping_info.mutable_task()->Add(std::move(task));

    is_end_graph_ = true;
    if (op_mapping_info.model_name_param_case() == aicpu::dump::OpMappingInfo::kModelName) {
      GELOGI("Add end_graph_info to aicpu, model_name is %s, task_id is %u, stream_id is %u",
             op_mapping_info.model_name().c_str(), end_graph_task_id_, end_graph_stream_id_);
      return;
    }
    GELOGI("Add end_graph_info to aicpu, task_id is %u, stream_id is %u", end_graph_task_id_, end_graph_stream_id_);
  }
}

void DataDumper::SetOpDebugIdToAicpu(uint32_t task_id, uint32_t stream_id, void *op_debug_addr,
                                     aicpu::dump::OpMappingInfo &op_mapping_info) {
  if (is_op_debug_) {
    GELOGI("add op_debug_info to aicpu, task_id is %u, stream_id is %u", task_id, stream_id);
    aicpu::dump::Task task;
    task.set_end_graph(false);
    task.set_task_id(task_id);
    task.set_stream_id(stream_id);
    task.mutable_op()->set_op_name(NODE_NAME_OP_DEBUG);
    task.mutable_op()->set_op_type(OP_TYPE_OP_DEBUG);

    // set output
    aicpu::dump::Output output;
    output.set_data_type(DT_UINT8);
    output.set_format(FORMAT_ND);

    output.mutable_shape()->add_dim(kOpDebugShape);

    output.set_original_name(NODE_NAME_OP_DEBUG);
    output.set_original_output_index(0);
    output.set_original_output_format(FORMAT_ND);
    output.set_original_output_data_type(DT_UINT8);
    // due to lhisi virtual addr bug, cannot use args now
    output.set_address(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(op_debug_addr)));
    output.set_size(kOpDebugSize);

    task.mutable_output()->Add(std::move(output));
    op_mapping_info.mutable_task()->Add(std::move(task));
  }
}

Status DataDumper::UnloadDumpInfo() {
  if (!load_flag_) {
    GELOGI("No need to UnloadDumpInfo.");
    load_flag_ = false;
    return SUCCESS;
  }

  GELOGI("UnloadDumpInfo start.");
  aicpu::dump::OpMappingInfo op_mapping_info;
  op_mapping_info.set_model_id(model_id_);
  op_mapping_info.set_flag(kAicpuUnloadFlag);

  for (const auto &op_iter : op_list_) {
    aicpu::dump::Task task;
    task.set_task_id(op_iter.task_id);
    op_mapping_info.mutable_task()->Add(std::move(task));
  }
  auto ret = ExecuteUnLoadDumpInfo(op_mapping_info);
  if (ret != SUCCESS) {
    GELOGE(ret, "Execute unload dump info failed");
    return ret;
  }
  return SUCCESS;
}

void DataDumper::PrintCheckLog(string &dump_list_key) {
  std::set<std::string> model_list = dump_properties_.GetAllDumpModel();
  if (model_list.empty()) {
    GELOGI("No model need dump.");
    return;
  }

  bool not_find_by_omname = model_list.find(om_name_) == model_list.end();
  bool not_find_by_modelname = model_list.find(model_name_) == model_list.end();
  dump_list_key = not_find_by_omname ? model_name_ : om_name_;
  GELOGI("%zu op need dump in known shape model %s.", op_list_.size(), dump_list_key.c_str());

  if (model_list.find(DUMP_ALL_MODEL) == model_list.end()) {
    if (not_find_by_omname && not_find_by_modelname) {
      std::string model_list_str;
      for (auto &model : model_list) {
        model_list_str += "[" + model + "].";
      }

      GELOGW("Model %s will not be set to dump, dump list: %s", dump_list_key.c_str(), model_list_str.c_str());
      return;
    }
  }

  std::set<std::string> config_dump_op_list = dump_properties_.GetPropertyValue(dump_list_key);
  std::set<std::string> dump_op_list;
  for (auto &inner_dump_info : op_list_) {
    // oplist value OpDescPtr is not nullptr
    dump_op_list.insert(inner_dump_info.op->GetName());
  }

  for (auto &dump_op : config_dump_op_list) {
    if (dump_op_list.find(dump_op) == dump_op_list.end()) {
      GELOGW("Op %s set to dump but not exist in model %s or not a valid op.", dump_op.c_str(), dump_list_key.c_str());
    }
  }
}

Status DataDumper::DumpExceptionInput(const OpDescInfo &op_desc_info, const string &dump_file) {
  GELOGI("Start to dump exception input");
  for (size_t i = 0; i < op_desc_info.input_addrs.size(); i++) {
    if (Debug::DumpDevMem(dump_file.data(), op_desc_info.input_addrs.at(i), op_desc_info.input_size.at(i)) != SUCCESS) {
      GELOGE(PARAM_INVALID, "Dump the %zu input data failed", i);
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

Status DataDumper::DumpExceptionOutput(const OpDescInfo &op_desc_info, const string &dump_file) {
  GELOGI("Start to dump exception output");
  for (size_t i = 0; i < op_desc_info.output_addrs.size(); i++) {
    if (Debug::DumpDevMem(dump_file.data(), op_desc_info.output_addrs.at(i), op_desc_info.output_size.at(i)) !=
        SUCCESS) {
      GELOGE(PARAM_INVALID, "Dump the %zu input data failed", i);
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}

Status DataDumper::DumpExceptionInfo(const std::vector<rtExceptionInfo> exception_infos) {
  GELOGI("Start to dump exception info");
  for (const rtExceptionInfo &iter : exception_infos) {
    OpDescInfo op_desc_info;
    if (GetOpDescInfo(iter.streamid, iter.taskid, op_desc_info)) {
      toolkit::dumpdata::DumpData dump_data;
      dump_data.set_version("2.0");
      dump_data.set_dump_time(GetNowTime());
      for (size_t i = 0; i < op_desc_info.input_format.size(); ++i) {
        toolkit::dumpdata::OpInput input;
        input.set_data_type(toolkit::dumpdata::OutputDataType(GetIrDataType(op_desc_info.input_data_type[i])));
        input.set_format(toolkit::dumpdata::OutputFormat(op_desc_info.input_format[i]));
        for (auto dim : op_desc_info.input_shape[i]) {
          input.mutable_shape()->add_dim(dim);
        }
        input.set_size(op_desc_info.input_size[i]);
        GELOGI("The input size int exception is %ld", op_desc_info.input_size[i]);
        dump_data.mutable_input()->Add(std::move(input));
      }
      for (size_t j = 0; j < op_desc_info.output_format.size(); ++j) {
        toolkit::dumpdata::OpOutput output;
        output.set_data_type(toolkit::dumpdata::OutputDataType(GetIrDataType(op_desc_info.output_data_type[j])));
        output.set_format(toolkit::dumpdata::OutputFormat(op_desc_info.output_format[j]));
        for (auto dim : op_desc_info.output_shape[j]) {
          output.mutable_shape()->add_dim(dim);
        }
        output.set_size(op_desc_info.output_size[j]);
        GELOGI("The output size int exception is %ld", op_desc_info.output_size[j]);
        dump_data.mutable_output()->Add(std::move(output));
      }
      uint64_t now_time = GetNowTime();
      string dump_file_path = "./" + op_desc_info.op_type + "." + op_desc_info.op_name + "." +
                              to_string(op_desc_info.task_id) + "." + to_string(now_time);
      uint64_t proto_size = dump_data.ByteSizeLong();
      unique_ptr<char[]> proto_msg(new (std::nothrow) char[proto_size]);
      bool ret = dump_data.SerializeToArray(proto_msg.get(), proto_size);
      if (!ret || proto_size == 0) {
        GELOGE(PARAM_INVALID, "Dump data proto serialize failed");
        return PARAM_INVALID;
      }

      GE_CHK_STATUS_RET(MemoryDumper::DumpToFile(dump_file_path.c_str(), &proto_size, sizeof(uint64_t)),
                        "Failed to dump proto size");
      GE_CHK_STATUS_RET(MemoryDumper::DumpToFile(dump_file_path.c_str(), proto_msg.get(), proto_size),
                        "Failed to dump proto msg");
      if (DumpExceptionInput(op_desc_info, dump_file_path) != SUCCESS) {
        GELOGE(PARAM_INVALID, "Dump exception input failed");
        return PARAM_INVALID;
      }

      if (DumpExceptionOutput(op_desc_info, dump_file_path) != SUCCESS) {
        GELOGE(PARAM_INVALID, "Dump exception output failed");
        return PARAM_INVALID;
      }
      GELOGI("Dump exception info SUCCESS");
    } else {
      GELOGE(PARAM_INVALID, "Get op desc info failed,task id:%u,stream id:%u", iter.taskid, iter.streamid);
      return PARAM_INVALID;
    }
  }
  return SUCCESS;
}
}  // namespace ge
