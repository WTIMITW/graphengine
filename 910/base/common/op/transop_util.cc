/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
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

#include "common/op/transop_util.h"

#include "common/types.h"
#include "graph/utils/type_utils.h"
#include "framework/common/debug/ge_log.h"

namespace {
const int32_t kInvalidTransopDataIndex = -1;
const int32_t kTransOpOutIndex = 0;
std::map<ge::DataType, ge::DataType> precision_loss_transfer_map = {
    {
        ge::DT_FLOAT, ge::DT_BOOL
    },
    {
        ge::DT_INT64, ge::DT_BOOL
    },
    {
        ge::DT_FLOAT16, ge::DT_BOOL
    }
};
}  // namespace

namespace ge {
TransOpUtil::TransOpUtil() {
  transop_index_map_ = {{TRANSDATA, 0},   {TRANSPOSE, 0},  {TRANSPOSED, 0}, {RESHAPE, 0},
                        {REFORMAT, 0},    {CAST, 0},       {SQUEEZE, 0},    {SQUEEZEV2, 0},
                        {UNSQUEEZEV2, 0}, {EXPANDDIMS, 0}, {SQUEEZEV3, 0},  {UNSQUEEZEV3, 0}};
}

TransOpUtil::~TransOpUtil() {}

TransOpUtil &TransOpUtil::Instance() {
  static TransOpUtil inst;
  return inst;
}

bool TransOpUtil::IsTransOp(const NodePtr &node) {
  if (node == nullptr) {
    return false;
  }
  return IsTransOp(node->GetType());
}

bool TransOpUtil::IsTransOp(const std::string &type) {
  return Instance().transop_index_map_.find(type) != Instance().transop_index_map_.end();
}

int32_t TransOpUtil::GetTransOpDataIndex(const NodePtr &node) {
  if (node == nullptr) {
    return kInvalidTransopDataIndex;
  }
  return GetTransOpDataIndex(node->GetType());
}

int32_t TransOpUtil::GetTransOpDataIndex(const std::string &type) {
  const std::map<std::string, int32_t>::const_iterator it = Instance().transop_index_map_.find(type);
  if (it != Instance().transop_index_map_.end()) {
    return it->second;
  }
  return kInvalidTransopDataIndex;
}

bool TransOpUtil::CheckPrecisionLoss(const ge::NodePtr &src_node) {
  const auto idx = TransOpUtil::GetTransOpDataIndex(src_node);
  const auto input_desc = src_node->GetOpDesc()->GetInputDesc(static_cast<uint32_t> (idx));
  const auto output_desc = src_node->GetOpDesc()->GetOutputDesc(static_cast<uint32_t> (kTransOpOutIndex));
  const auto src_dtype = input_desc.GetDataType();
  const auto dst_dtype = output_desc.GetDataType();
  const std::map<ge::DataType, ge::DataType>::const_iterator iter = precision_loss_transfer_map.find(src_dtype);
  if ((iter != precision_loss_transfer_map.end()) && (iter->second == dst_dtype)) {
    GELOGW("Node %s transfer data type from %s to %s ,it will cause precision loss. ignore pass.",
           src_node->GetName().c_str(),
           TypeUtils::DataTypeToSerialString(src_dtype).c_str(),
           TypeUtils::DataTypeToSerialString(dst_dtype).c_str());
    return false;
  }
  return true;
}

std::string TransOpUtil::TransopMapToString() {
  std::string buffer;
  for (auto &key : Instance().transop_index_map_) {
    buffer += key.first + " ";
  }
  return buffer;
}
}  // namespace ge
