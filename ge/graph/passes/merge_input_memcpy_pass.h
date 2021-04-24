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

#ifndef GE_GRAPH_PASSES_MERGE_ADD_INPUT_MEMCPY_PASS_H_
#define GE_GRAPH_PASSES_MERGE_ADD_INPUT_MEMCPY_PASS_H_

#include "inc/graph_pass.h"

namespace ge {
class MergeInputMemcpyPass : public GraphPass {
 public:
  Status Run(ComputeGraphPtr graph);

 private:
  ///
  /// @brief Add MemcpyAsync Op as Merge in_node
  /// @param [in] graph
  /// @param [in] node
  /// @param [in] multi_batch_flag
  /// @return Status
  ///
  Status AddMemcpyAsyncNodes(const ComputeGraphPtr &graph, const NodePtr &node, bool multi_batch_flag);

  ///
  /// @brief Add MemcpyAsync Node
  /// @param [in] graph
  /// @param [in] name
  /// @param [in] out_data_anchor
  /// @param [in] multi_batch_flag
  /// @return ge::NodePtr
  ///
  NodePtr CreateMemcpyAsyncNode(const ComputeGraphPtr &graph, const std::string &name,
                                const OutDataAnchorPtr &out_data_anchor, bool multi_batch_flag);

  ///
  /// @brief Mark force unknown shape for Switch node
  /// @param [in] merge node
  /// @param [out] switch_groups
  /// @return
  ///
  void CollectSwitchGroup(const NodePtr &node, std::unordered_map<NodePtr, std::vector<NodePtr>> &switch_groups);

  ///
  /// @brief Mark force unknown shape for Switch node
  /// @param [in] switch_groups
  /// @return
  ///
  void MarkUnknownForSwitch(const std::unordered_map<NodePtr, std::vector<NodePtr>> &switch_groups);
};
}  // namespace ge
#endif  // GE_GRAPH_PASSES_MERGE_ADD_INPUT_MEMCPY_PASS_H_
