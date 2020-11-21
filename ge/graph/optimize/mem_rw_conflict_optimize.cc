/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <vector>

#include "common/ge/ge_util.h"
#include "graph/common/omg_util.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/optimize/graph_optimize.h"
#include "graph/utils/graph_utils.h"
#include "graph/utils/node_utils.h"

namespace {
using namespace ge;
const int kIdentityAnchorIndex = 0;
// rw type of input.
enum class InputRWType {
  kReadOnly,        // Normal op input only read
  kWriteable,       // Op like Assign/ApplyMomentum
  kScopeWriteable,  // Op like hcom_allreduce, it will modify input ,but not expect take effect on pre ouput
  kInvalidRWType
};
// rw type of output
enum class OutputRWType {
  kReadOnly,   // 1.const output  2.not ref output but has several peer output
  kSoftRead,   // not ref output but only has one output node
  kWriteable,  // ref output. Like Assign/ApplyMomentum
  kInvalidRWType
};

// input and output rw_type of one node. key is anchor_idx, value is rw_type
struct NodeInputOutputRWType {
  map<uint32_t, InputRWType> input_rw_type_map;
  map<uint32_t, OutputRWType> output_rw_type_map;
};
// input and output rw_type of node in current graph
thread_local map<string, NodeInputOutputRWType> node_rwtype_map_;

///
/// @brief Convert input rw_type enum to string. For log print.
/// @param rw_type
/// @return rw_type_name
///
static std::string InputRWTypeToSerialString(InputRWType rw_type) {
  const static char *names[4] = {"ReadOnly", "Writeable", "ScopeWriteable", "InvalidRWType"};
  return names[static_cast<int>(rw_type)];
}

///
/// @brief Convert output rw_type enum to string. For log print.
/// @param rw_type
/// @return rw_type_name
///
static std::string OutputRWTypeToSerialString(OutputRWType rw_type) {
  const static char *names[4] = {"ReadOnly", "SoftRead", "Writeable", "InvalidRWType"};
  return names[static_cast<int>(rw_type)];
}

OutputRWType GetSingleNodeOutputRWTypeByIndex(const Node &node, uint32_t index) {
  auto op_desc = node.GetOpDesc();
  if (op_desc == nullptr) {
    return OutputRWType::kInvalidRWType;
  }
  if (op_desc->GetType() == VARIABLE) {
    return OutputRWType::kWriteable;
  }
  // check if it is ref output
  auto input_names = op_desc->GetAllInputName();
  for (auto &input_name_2_idx : input_names) {
    if (op_desc->GetOutputNameByIndex(index) == input_name_2_idx.first) {
      return OutputRWType::kWriteable;
    }
  }
  // check if it is ref switch
  std::string type;
  if ((node.GetType() == FRAMEWORK_OP_TYPE) && AttrUtils::GetStr(op_desc, ATTR_NAME_FRAMEWORK_ORIGINAL_TYPE, type)
      && (type == REFSWITCH)) {
    return OutputRWType::kWriteable;
  }

  if (op_desc->GetType() == CONSTANT || op_desc->GetType() == CONSTANTOP) {
    return OutputRWType::kReadOnly;
  }
  auto out_data_anchor = node.GetOutDataAnchor(index);
  if (out_data_anchor == nullptr) {
    return OutputRWType::kInvalidRWType;
  }
  if (out_data_anchor->GetPeerInDataNodesSize() > 1) {
    return OutputRWType::kReadOnly;
  } else {
    return OutputRWType::kSoftRead;
  }
}

///
/// @brief Get input rw_type of one node with sub graph. It will return rw_type after solve conflict scene.
/// @param rw_type_set
/// @return
///
InputRWType GetInputRwTypeInConflict(const std::set<int> &rw_type_set) {
  // for input rw type calc
  int total_rw_type = 0;
  for (const auto rw : rw_type_set) {
    total_rw_type += rw;
  }

  switch (total_rw_type) {
    case 0:
      return InputRWType::kReadOnly;  // all input rw type is readonly
    case 2:
      return InputRWType::kScopeWriteable;  // readonly 2 scope_writeable
    case 3:
      return InputRWType::kWriteable;  // all input rw type is writeable or readonly 2 writeable
    case 5:
      return InputRWType::kInvalidRWType;  // writeable 2 scope_writeable
    default:
      return InputRWType::kInvalidRWType;
  }
}

NodePtr CreateIdentityAfterSrcNode(const Node &src_node, int out_anchor_idx) {
  if (src_node.GetOpDesc() == nullptr) {
    return nullptr;
  }
  static std::atomic_long identity_num(0);
  auto next_num = identity_num.fetch_add(1);
  // 1. create new identity op desc
  string identity_name = src_node.GetName() + "_" + IDENTITY + std::to_string(next_num);
  auto identity_opdesc = MakeShared<OpDesc>(identity_name, IDENTITY);
  if (identity_opdesc == nullptr) {
    GELOGE(OUT_OF_MEMORY, "Failed to insert identity node, name %s", identity_name.c_str());
    return nullptr;
  }
  auto data_desc = src_node.GetOpDesc()->GetOutputDesc(out_anchor_idx);
  // 2. add input_desc & output_desc for new identity
  Status ret = identity_opdesc->AddInputDesc("x", data_desc);
  if (ret != SUCCESS) {
    GELOGE(ret, "Add Input desc failed for new identity %s.", identity_name.c_str());
    return nullptr;
  }
  ret = identity_opdesc->AddOutputDesc("y", data_desc);
  if (ret != SUCCESS) {
    GELOGE(ret, "Add Output desc failed for new Identity %s.", identity_name.c_str());
    return nullptr;
  }
  GELOGI("Insert new Identity node %s.", identity_name.c_str());
  auto graph = src_node.GetOwnerComputeGraph();
  if (graph == nullptr) {
    GELOGE(GRAPH_PARAM_INVALID, "Node %s owner compute graph is null.", src_node.GetName().c_str());
    return nullptr;
  }
  return graph->AddNode(identity_opdesc);
}

OutputRWType GetOutputRWTypeByIndex(const Node &node, uint32_t index) {
  auto op_desc = node.GetOpDesc();
  if (op_desc == nullptr) {
    return OutputRWType::kInvalidRWType;
  }
  if (op_desc->GetType() == WHILE) {
    return OutputRWType::kSoftRead;
  }
  vector<string> subgraph_names = op_desc->GetSubgraphInstanceNames();
  if (subgraph_names.empty()) {
    // single node without sub graph
    return GetSingleNodeOutputRWTypeByIndex(node, index);
  } else {
    // node with sub graph
    auto output_node_vec = NodeUtils::GetSubgraphOutputNodes(node);
    auto output_rw_type = OutputRWType::kInvalidRWType;
    if (output_node_vec.size() == 1) {
      // find rw type from map.
      auto iter = node_rwtype_map_.find(output_node_vec.at(0)->GetName());
      if (iter == node_rwtype_map_.end()) {
        GELOGW("Can not find rw type of node %s from map.It could take some effect on following preprocess.",
               output_node_vec.at(0)->GetName().c_str());
        return OutputRWType::kInvalidRWType;
      }
      auto index_2_output_rw_type = iter->second.output_rw_type_map.find(index);
      if (index_2_output_rw_type == iter->second.output_rw_type_map.end()) {
        GELOGW("Can not find rw type of node %s from map.It could take some effect on following preprocess.",
               output_node_vec.at(0)->GetName().c_str());
        return OutputRWType::kInvalidRWType;
      }
      output_rw_type = index_2_output_rw_type->second;
    } else {
      output_rw_type = OutputRWType::kSoftRead;
    }
    // check peer input
    auto out_data_anchor = node.GetOutDataAnchor(index);
    if (out_data_anchor == nullptr) {
      return OutputRWType::kInvalidRWType;
    }
    if (out_data_anchor->GetPeerInDataNodesSize() > 1) {
      return OutputRWType::kReadOnly;
    } else {
      return output_rw_type;
    }
  }
}

InputRWType GetSingleNodeInputRWTypeByIndex(const Node &node, uint32_t index) {
  auto op_desc = node.GetOpDesc();
  if (op_desc == nullptr) {
    return InputRWType::kInvalidRWType;
  }
  if (op_desc->GetType() == HCOMALLREDUCE || op_desc->GetType() == HCOMALLGATHER
      || op_desc->GetType() == HCOMREDUCESCATTER) {
    return InputRWType::kScopeWriteable;
  }
  // check if it is ref input
  auto output_names = op_desc->GetAllOutputName();
  for (auto &output_name_2_idx : output_names) {
    if (op_desc->GetInputNameByIndex(index) == output_name_2_idx.first) {
      return InputRWType::kWriteable;
    }
  }
  // check if it is ref switch
  std::string type;
  if ((node.GetType() == FRAMEWORK_OP_TYPE) && (AttrUtils::GetStr(op_desc, ATTR_NAME_FRAMEWORK_ORIGINAL_TYPE, type))
      && (type == REFSWITCH) && (index == 0)) {
    return InputRWType::kWriteable;
  }

  return InputRWType::kReadOnly;
}

InputRWType GetInputRWTypeByIndex(const Node &node, uint32_t index) {
  auto op_desc = node.GetOpDesc();
  if (op_desc == nullptr) {
    return InputRWType::kInvalidRWType;
  }
  if (op_desc->GetType() == WHILE) {
    return InputRWType::kScopeWriteable;
  }
  vector<string> subgraph_names = op_desc->GetSubgraphInstanceNames();
  if (subgraph_names.empty()) {
    // single node without sub graph
    return GetSingleNodeInputRWTypeByIndex(node, index);
  } else {
    // node with sub graph
    std::set<int> node_rw_type_set;
    auto data_node_vec = NodeUtils::GetSubgraphDataNodesByIndex(node, index);
    // get all input data node in subgraph
    std::set<int> anchor_rw_type_set;
    for (const auto &data_node : data_node_vec) {
      // Data only has 1 out data anchor. Here just take first out data anchor. And index 0 is valid.
      auto out_data_anchor = data_node->GetOutDataAnchor(0);
      if (out_data_anchor == nullptr) {
        continue;
      }
      auto data_op_desc = data_node->GetOpDesc();
      if (data_op_desc == nullptr) {
        continue;
      }
      // find rw type from map.
      auto iter = node_rwtype_map_.find(data_op_desc->GetName());
      if (iter == node_rwtype_map_.end()) {
        GELOGW("Can not find rw type of node %s from map.It could take some effect on following preprocess.",
               data_op_desc->GetName().c_str());
        return InputRWType::kInvalidRWType;
      }
      auto input_rw_type = iter->second.input_rw_type_map.find(out_data_anchor->GetIdx());
      if (input_rw_type == iter->second.input_rw_type_map.end()) {
        GELOGW("Can not find rw type of node %s from map.It could take some effect on following preprocess.",
               data_op_desc->GetName().c_str());
        return InputRWType::kInvalidRWType;
      }
      anchor_rw_type_set.emplace(static_cast<int>(input_rw_type->second));
    }
    return GetInputRwTypeInConflict(anchor_rw_type_set);
  }
}
Status MarkRWTypeForSubgraph(const ComputeGraphPtr &sub_graph) {
  for (const auto &node : sub_graph->GetDirectNode()) {
    GE_CHECK_NOTNULL(node);
    GE_CHECK_NOTNULL(node->GetOpDesc());
    std::set<int> anchor_rw_type_set;
    if (node->GetType() == DATA) {
      // calc all input_rw_type of peer output , as input_rw_type of DATA. Index 0 is valid.
      auto anchor_2_node_vec = NodeUtils::GetOutDataNodesWithAnchorByIndex(*node, 0);
      for (const auto anchor_2_node_pair : anchor_2_node_vec) {
        auto input_rw_type = GetInputRWTypeByIndex(*anchor_2_node_pair.second, anchor_2_node_pair.first->GetIdx());
        GELOGD("Input rw type of Node %s %dth input anchor is %s", anchor_2_node_pair.second->GetName().c_str(),
               anchor_2_node_pair.first->GetIdx(), InputRWTypeToSerialString(input_rw_type).c_str());
        anchor_rw_type_set.emplace(static_cast<int>(input_rw_type));
      }
      auto anchor_rw_type = GetInputRwTypeInConflict(anchor_rw_type_set);
      GELOGD("Input rw type of Node %s is %s", node->GetName().c_str(),
             InputRWTypeToSerialString(anchor_rw_type).c_str());
      map<uint32_t, InputRWType> input_rw_type_map{std::make_pair(0, anchor_rw_type)};
      NodeInputOutputRWType data_rw_type{input_rw_type_map};
      node_rwtype_map_.emplace(std::make_pair(node->GetName(), data_rw_type));
    }

    if (node->GetType() == NETOUTPUT) {
      // calc all output_rw_type of peer input , as output_rw_type of DATA
      map<uint32_t, OutputRWType> output_rw_type_map;
      for (const auto &in_data_anchor : node->GetAllInDataAnchors()) {
        GE_CHECK_NOTNULL(in_data_anchor);
        auto pre_out_anchor = in_data_anchor->GetPeerOutAnchor();
        GE_CHECK_NOTNULL(pre_out_anchor);
        auto pre_node = pre_out_anchor->GetOwnerNode();
        GE_CHECK_NOTNULL(pre_node);

        auto pre_output_rw_type = GetOutputRWTypeByIndex(*pre_node, pre_out_anchor->GetIdx());
        GELOGD("Output rw type of Node %s %dth output anchor is %s", pre_node->GetName().c_str(),
               pre_out_anchor->GetIdx(), OutputRWTypeToSerialString(pre_output_rw_type).c_str());
        if (pre_output_rw_type == OutputRWType::kWriteable) {
          // insert identity
          auto identity_node = CreateIdentityAfterSrcNode(*pre_node, pre_out_anchor->GetIdx());
          GE_CHECK_NOTNULL(identity_node);
          auto ret = GraphUtils::InsertNodeBetweenDataAnchors(pre_out_anchor, in_data_anchor, identity_node);
          if (ret != SUCCESS) {
            GELOGE(ret, "Fail to insert identity");
            return ret;
          }
          GELOGI("InsertNode %s between %s and %s successfully.", identity_node->GetName().c_str(),
                 pre_node->GetName().c_str(), node->GetName().c_str());
        }
        output_rw_type_map.emplace(std::make_pair(in_data_anchor->GetIdx(), OutputRWType::kSoftRead));
      }
      NodeInputOutputRWType output_rw_type{{}, output_rw_type_map};
      node_rwtype_map_.emplace(std::make_pair(node->GetName(), output_rw_type));
    }
  }
  return SUCCESS;
}
///
/// @brief Reverse traversal all subgraph and mark rw_type for Data/Netoutput.
/// @param sub_graph_vecgs
///
Status MarkRWTypeForAllSubgraph(const vector<ComputeGraphPtr> &sub_graph_vec) {
  for (auto iter = sub_graph_vec.rbegin(); iter != sub_graph_vec.rend(); ++iter) {
    auto parent_node = (*iter)->GetParentNode();
    if (parent_node == nullptr) {
      GELOGD("Current sub graph has no parent node. Ignore it.");
      continue;
    }
    if (parent_node->GetType() == WHILE) {
      continue;
    }
    auto ret = MarkRWTypeForSubgraph(*iter);
    if (ret != SUCCESS) {
      return ret;
    }
  }
  return SUCCESS;
}

///
/// @brief Check identity is near subgraph.
///    Eg. As output of Data node in subgraph
///        or as input of Netoutput of subgraph
///        or as input of one node with subgraph
///        or as output of one node with subgraph
/// @param node
/// @return is_near_subgraph
///
bool CheckIdentityIsNearSubgraph(const Node &node) {
  for (const auto &in_node : node.GetInDataNodes()) {
    auto in_node_opdesc = in_node->GetOpDesc();
    if (in_node_opdesc == nullptr) {
      continue;
    }
    // near entrance of subgraph
    if (in_node->GetType() == DATA && NodeUtils::IsSubgraphInput(in_node)) {
      return true;
    }
    // near subgraph
    if (!in_node_opdesc->GetSubgraphInstanceNames().empty()) {
      return true;
    }
  }

  for (const auto &out_node : node.GetOutDataNodes()) {
    auto out_node_opdesc = out_node->GetOpDesc();
    if (out_node_opdesc == nullptr) {
      continue;
    }
    // near output of subgraph
    if (out_node->GetType() == NETOUTPUT && NodeUtils::IsSubgraphOutput(out_node)) {
      return true;
    }
    // near subgraph
    if (!out_node_opdesc->GetSubgraphInstanceNames().empty()) {
      return true;
    }
  }
  return false;
}
enum ConflictResult { DO_NOTHING, WRONG_GRAPH, INSERT_IDENTITY };
vector<vector<ConflictResult>> output_2_input_rwtype = {{DO_NOTHING, WRONG_GRAPH, INSERT_IDENTITY},
                                                        {DO_NOTHING, WRONG_GRAPH, DO_NOTHING},
                                                        {DO_NOTHING, DO_NOTHING, INSERT_IDENTITY}};
ConflictResult GetConflictResultBetweenNode(const OutputRWType output_rw_type, const InputRWType input_rw_type) {
  if (output_rw_type == OutputRWType::kInvalidRWType || input_rw_type == InputRWType::kInvalidRWType) {
    return WRONG_GRAPH;
  }
  auto n = static_cast<int>(output_rw_type);
  auto m = static_cast<int>(input_rw_type);
  // no need to check index or container, because container and index is all defined.
  return output_2_input_rwtype[n][m];
}

///
/// @brief Keep identity_node which near subgraph or has multi output
/// @param node
/// @return
///
Status RemoveNoUseIdentity(const NodePtr &node) {
  if (node->GetInDataNodes().empty() || node->GetOutDataNodesSize() > 1) {
    return SUCCESS;
  }
  if (node->GetOutDataNodesSize() == 1 && node->GetOutDataNodes().at(0)->GetType() == STREAMMERGE) {
    return SUCCESS;
  }
  if (CheckIdentityIsNearSubgraph(*node)) {
    return SUCCESS;
  }
  GE_CHECK_NOTNULL(node->GetInDataAnchor(kIdentityAnchorIndex));
  auto pre_out_anchor = node->GetInDataAnchor(kIdentityAnchorIndex)->GetPeerOutAnchor();
  GE_CHECK_NOTNULL(pre_out_anchor);
  auto pre_node = pre_out_anchor->GetOwnerNode();
  auto pre_output_rw_type = GetOutputRWTypeByIndex(*pre_node, pre_out_anchor->GetIdx());

  auto anchor_2_outnode_vec = NodeUtils::GetOutDataNodesWithAnchorByIndex(*node, kIdentityAnchorIndex);
  ConflictResult conflict_result = WRONG_GRAPH;
  if (!anchor_2_outnode_vec.empty()) {
    auto anchor_2_outnode = anchor_2_outnode_vec.at(0);
    auto peer_input_rw_type = GetInputRWTypeByIndex(*anchor_2_outnode.second, anchor_2_outnode.first->GetIdx());

    GELOGD("Pre Node %s %dth output rw type is %s, peer node %s %dth input rw type is %s.", pre_node->GetName().c_str(),
           pre_out_anchor->GetIdx(), OutputRWTypeToSerialString(pre_output_rw_type).c_str(),
           anchor_2_outnode.second->GetName().c_str(), anchor_2_outnode.first->GetIdx(),
           InputRWTypeToSerialString(peer_input_rw_type).c_str());
    conflict_result = GetConflictResultBetweenNode(pre_output_rw_type, peer_input_rw_type);
  } else {
    // identity node has no out data node, it can be removed
    conflict_result = DO_NOTHING;
  }
  if (conflict_result != DO_NOTHING) {
    return SUCCESS;
  }

  GELOGI("No need insert Identity. Node %s need to remove.", node->GetName().c_str());
  auto ret = GraphUtils::IsolateNode(node, {0});
  if (ret != SUCCESS) {
    GELOGE(ret, "Fail to isolate node %s.", node->GetName().c_str());
    return ret;
  }
  ret = GraphUtils::RemoveNodeWithoutRelink(node->GetOwnerComputeGraph(), node);
  if (ret != SUCCESS) {
    GELOGE(ret, "Fail to isolate node %s.", node->GetName().c_str());
    return ret;
  }
  GELOGI("Pre node is %s and %dth output rw type is %s. Isolate and remove Identity node %s.",
         pre_node->GetName().c_str(), pre_out_anchor->GetIdx(), OutputRWTypeToSerialString(pre_output_rw_type).c_str(),
         node->GetName().c_str());
  return SUCCESS;
}

Status SplitIdentityAlongAnchor(const OutDataAnchorPtr &out_data_anchor, const InDataAnchorPtr &peer_in_data_anchor,
                                const OutDataAnchorPtr &pre_out_data_anchor, NodePtr &pre_node) {
  // 1.check peer in node RW type.
  GE_CHECK_NOTNULL(peer_in_data_anchor);
  auto peer_in_data_node = peer_in_data_anchor->GetOwnerNode();
  GE_CHECK_NOTNULL(peer_in_data_node);
  auto input_rw_type = GetInputRWTypeByIndex(*peer_in_data_node, peer_in_data_anchor->GetIdx());
  auto ret = out_data_anchor->Unlink(peer_in_data_anchor);
  auto old_identity = out_data_anchor->GetOwnerNode();
  if (ret != SUCCESS) {
    GELOGE(ret, "Failed to unlink from %s %dth out to %s.", old_identity->GetName().c_str(), out_data_anchor->GetIdx(),
           peer_in_data_anchor->GetOwnerNode()->GetName().c_str());
    return ret;
  }
  if (input_rw_type == InputRWType::kScopeWriteable || input_rw_type == InputRWType::kWriteable) {
    auto new_identity = CreateIdentityAfterSrcNode(*pre_node, pre_out_data_anchor->GetIdx());
    GE_CHECK_NOTNULL(new_identity);
    if (GraphUtils::AddEdge(pre_out_data_anchor, new_identity->GetInDataAnchor(kIdentityAnchorIndex)) != SUCCESS
        || GraphUtils::AddEdge(new_identity->GetOutDataAnchor(kIdentityAnchorIndex), peer_in_data_anchor) != SUCCESS) {
      GELOGE(INTERNAL_ERROR, "Failed to insert Identity between node %s and %s",
             pre_out_data_anchor->GetOwnerNode()->GetName().c_str(),
             peer_in_data_anchor->GetOwnerNode()->GetName().c_str());
      return INTERNAL_ERROR;
    }

    // 2. copy in-control-edge from dst to Identity
    if (GraphUtils::CopyInCtrlEdges(peer_in_data_node, new_identity) != SUCCESS) {
      GELOGE(INTERNAL_ERROR, "Failed to copy in_control edges from node %s to %s", peer_in_data_node->GetName().c_str(),
             new_identity->GetName().c_str());
      return INTERNAL_ERROR;
    }
    GELOGI("Node %s intput rw type is %s. Insert Identity between %s and %s.", peer_in_data_node->GetName().c_str(),
           InputRWTypeToSerialString(input_rw_type).c_str(), pre_out_data_anchor->GetOwnerNode()->GetName().c_str(),
           peer_in_data_anchor->GetOwnerNode()->GetName().c_str());
  } else {
    // copy control edge to pre and peer node
    if (GraphUtils::CopyInCtrlEdges(old_identity, peer_in_data_node) != SUCCESS
        || GraphUtils::CopyOutCtrlEdges(old_identity, pre_node) != SUCCESS) {
      GELOGW("Fail to copy control edge from node %s.", old_identity->GetName().c_str());
      return FAILED;
    }
    // link identity pre node to next node directly
    if (GraphUtils::AddEdge(pre_out_data_anchor, peer_in_data_anchor) != SUCCESS) {
      GELOGW("Fail to link data edge from node %s to %s.", pre_out_data_anchor->GetOwnerNode()->GetName().c_str(),
             peer_in_data_anchor->GetOwnerNode()->GetName().c_str());
      return FAILED;
    }
    GELOGI("Node %s input rw type is %s, link data edge from Identity input node %s to out node %s directly.",
           peer_in_data_node->GetName().c_str(), InputRWTypeToSerialString(input_rw_type).c_str(),
           pre_node->GetName().c_str(), peer_in_data_node->GetName().c_str());
  }
  return SUCCESS;
}

Status SplitIdentity(const NodePtr &node) {
  GE_CHECK_NOTNULL(node);
  auto out_data_anchor = node->GetOutDataAnchor(kIdentityAnchorIndex);
  GE_CHECK_NOTNULL(out_data_anchor);
  if (out_data_anchor->GetPeerInDataNodesSize() <= 1) {
    return SUCCESS;
  }
  // get pre node and next node of identity
  GE_CHECK_NOTNULL(node->GetInDataAnchor(kIdentityAnchorIndex));
  auto pre_out_data_anchor = node->GetInDataAnchor(kIdentityAnchorIndex)->GetPeerOutAnchor();
  GE_CHECK_NOTNULL(pre_out_data_anchor);
  auto pre_node = pre_out_data_anchor->GetOwnerNode();
  GE_CHECK_NOTNULL(pre_node);
  for (const auto &peer_in_data_anchor : out_data_anchor->GetPeerInDataAnchors()) {
    Status ret = SplitIdentityAlongAnchor(out_data_anchor, peer_in_data_anchor, pre_out_data_anchor, pre_node);
    if (ret != SUCCESS) {
      GELOGE(ret, "Split identity node along anchor failed.");
      return ret;
    }
  }
  // 2.isolate Identity node with no data output
  if (node->GetOutDataNodesSize() == 0) {
    Status ret = GraphUtils::IsolateNode(node, {});
    if (ret != SUCCESS) {
      GELOGE(FAILED, "IsolateAndDelete identity node %s.", node->GetName().c_str());
      return FAILED;
    }
    ret = GraphUtils::RemoveNodeWithoutRelink(node->GetOwnerComputeGraph(), node);
    if (ret != SUCCESS) {
      GELOGE(FAILED, "IsolateAndDelete identity node %s.", node->GetName().c_str());
      return FAILED;
    }
    GELOGI("IsolateAndDelete identity node %s.", node->GetName().c_str());
  }
  return SUCCESS;
}

Status InsertIdentityAsNeeded(const NodePtr &node) {
  auto op_desc = node->GetOpDesc();
  GE_CHECK_NOTNULL(op_desc);
  if (node->GetOutDataNodesSize() == 0) {
    return SUCCESS;
  }
  for (const auto &out_data_anchor : node->GetAllOutDataAnchors()) {
    GE_CHECK_NOTNULL(out_data_anchor);
    auto output_rw_type = GetOutputRWTypeByIndex(*node, out_data_anchor->GetIdx());
    for (const auto &peer_in_data_anchor : out_data_anchor->GetPeerInDataAnchors()) {
      GE_CHECK_NOTNULL(peer_in_data_anchor);
      auto peer_in_node = peer_in_data_anchor->GetOwnerNode();
      GE_CHECK_NOTNULL(peer_in_node);
      auto input_rw_type = GetInputRWTypeByIndex(*peer_in_node, peer_in_data_anchor->GetIdx());
      GELOGD("Node %s output rw type is %s, Node %s input rw type is %s", node->GetName().c_str(),
             OutputRWTypeToSerialString(output_rw_type).c_str(), peer_in_node->GetName().c_str(),
             InputRWTypeToSerialString(input_rw_type).c_str());
      auto conflict_result = GetConflictResultBetweenNode(output_rw_type, input_rw_type);
      switch (conflict_result) {
        case DO_NOTHING:
        case WRONG_GRAPH:
          GELOGD("No need insert Identity.");
          continue;
        case INSERT_IDENTITY:
          auto identity_node = CreateIdentityAfterSrcNode(*node, out_data_anchor->GetIdx());
          if (identity_node == nullptr) {
            GELOGE(FAILED, "Create identity node failed.");
            return FAILED;
          }
          auto ret = GraphUtils::InsertNodeBetweenDataAnchors(out_data_anchor, peer_in_data_anchor, identity_node);
          if (ret != GRAPH_SUCCESS) {
            GELOGE(INTERNAL_ERROR, "Failed to insert reshape between node %s and %s", node->GetName().c_str(),
                   peer_in_node->GetName().c_str());
            return INTERNAL_ERROR;
          }
          GELOGI("Insert Identity between %s and %s to handle memory conflict.", node->GetName().c_str(),
                 peer_in_node->GetName().c_str());
          continue;
      }
    }
  }
  return SUCCESS;
}
Status HandleAllreduceDuplicateInput(ComputeGraphPtr &compute_graph) {
   for (const auto &node : compute_graph->GetDirectNode()) {
     if (node->GetType() == HCOMALLREDUCE) {
       std::set<OutDataAnchorPtr> pre_out_anchor_set;
       for (const auto &in_data_anchor : node->GetAllInDataAnchors()) {
         auto pre_out_anchor = in_data_anchor->GetPeerOutAnchor();
         GE_CHECK_NOTNULL(pre_out_anchor);
         if (pre_out_anchor_set.find(pre_out_anchor) == pre_out_anchor_set.end()) {
           pre_out_anchor_set.emplace(pre_out_anchor);
           continue;
         }
         // need insert identity
         auto pre_node = pre_out_anchor->GetOwnerNode();
         auto identity_node = CreateIdentityAfterSrcNode(*pre_node, pre_out_anchor->GetIdx());
         GE_CHECK_NOTNULL(identity_node);
         auto ret = GraphUtils::InsertNodeBetweenDataAnchors(pre_out_anchor, in_data_anchor, identity_node);
         GE_CHK_STATUS_RET(ret, "Fail to insert identity.");
         GELOGI("InsertNode %s between %s and %s successfully.", identity_node->GetName().c_str(),
               pre_node->GetName().c_str(), node->GetName().c_str())
       }
     }
   }
   return SUCCESS;
}
}  // namespace

namespace ge {
Status GraphOptimize::CheckRWConflict(ComputeGraphPtr &compute_graph, bool &has_conflict) {
  node_rwtype_map_.clear();
  auto sub_graph_vec = compute_graph->GetAllSubgraphs();
  if (sub_graph_vec.empty()) {
    GELOGD("No sub graph here. Ignore memory conflict handle.");
    return SUCCESS;
  }
  // 1.loop all subgraph, mark rw type from inside to outside
  Status ret = MarkRWTypeForAllSubgraph(sub_graph_vec);
  if (ret != SUCCESS) {
    GELOGE(ret, "Fail to mark rw type for subgraph.");
    return ret;
  }
  has_conflict = false;
  for (const auto &node : compute_graph->GetAllNodes()) {
    auto op_desc = node->GetOpDesc();
    GE_CHECK_NOTNULL(op_desc);
    if (node->GetOutDataNodesSize() == 0) {
      return SUCCESS;
    }
    if (node->GetType() == WHILE) {
      return SUCCESS;
    }
    for (const auto &out_data_anchor : node->GetAllOutDataAnchors()) {
      GE_CHECK_NOTNULL(out_data_anchor);
      auto output_rw_type = GetOutputRWTypeByIndex(*node, out_data_anchor->GetIdx());
      for (const auto &peer_in_data_anchor : out_data_anchor->GetPeerInDataAnchors()) {
        GE_CHECK_NOTNULL(peer_in_data_anchor);
        auto peer_in_node = peer_in_data_anchor->GetOwnerNode();
        GE_CHECK_NOTNULL(peer_in_node);
        if (peer_in_node->GetType() == WHILE) {
          return SUCCESS;
        }
        auto input_rw_type = GetInputRWTypeByIndex(*peer_in_node, peer_in_data_anchor->GetIdx());
        auto conflict_result = GetConflictResultBetweenNode(output_rw_type, input_rw_type);
        switch (conflict_result) {
          case DO_NOTHING:
            GELOGD("No rw conflict.");
            continue;
          case WRONG_GRAPH:
            has_conflict = true;
            GELOGI("Node %s output rw type is %s, next node %s input_rw_type is %s.It is wrong graph.",
                   node->GetName().c_str(), OutputRWTypeToSerialString(output_rw_type).c_str(),
                   peer_in_node->GetName().c_str(), InputRWTypeToSerialString(input_rw_type).c_str());
            return SUCCESS;
          case INSERT_IDENTITY:
            GELOGD("There is rw conflict. It will handle later.");
            continue;
        }
      }
    }
  }
  return SUCCESS;
}
Status GraphOptimize::HandleMemoryRWConflict(ComputeGraphPtr &compute_graph) {
  GE_DUMP(compute_graph, "BeforeHandleMemConflict");
  node_rwtype_map_.clear();
  auto sub_graph_vec = compute_graph->GetAllSubgraphs();
  if (sub_graph_vec.empty()) {
    // only root graph, to handle allreduce servral input from one output anchor
    return HandleAllreduceDuplicateInput(compute_graph);
  }

  // 1.loop all subgraph, mark rw type from inside to outside
  Status ret = MarkRWTypeForAllSubgraph(sub_graph_vec);
  if (ret != SUCCESS) {
    GELOGE(ret, "Fail to mark rw type for subgraph.");
    return ret;
  }
  // 2.loop all node, including node in subgraph and handle memory rw conflict
  for (auto &node : compute_graph->GetAllNodes()) {
    // ignore while subgraph node
    const auto parent_node = node->GetOwnerComputeGraph()->GetParentNode();
    if ((parent_node != nullptr) && (kWhileOpTypes.count(parent_node->GetType()) > 0)) {
      continue;
    }
    // ignore data / netoutput of subgraph
    if (node->GetType() == DATA && AttrUtils::HasAttr(node->GetOpDesc(), ATTR_NAME_PARENT_NODE_INDEX)) {
      continue;
    }
    if (node->GetType() == NETOUTPUT && AttrUtils::HasAttr(node->GetOpDesc(), ATTR_NAME_PARENT_NODE_INDEX)) {
      continue;
    }
    if (node->GetType() == IDENTITY || node->GetType() == READVARIABLEOP) {
      // split identity
      ret = SplitIdentity(node);
      if (ret != SUCCESS) {
        GELOGE(ret, "Fail to split identity node %s.", node->GetName().c_str());
        return ret;
      }
      // remove no use identity
      ret = RemoveNoUseIdentity(node);
      if (ret != SUCCESS) {
        GELOGE(ret, "Fail to remove useless identity node %s.", node->GetName().c_str());
        return ret;
      }
    }
    // insert Identity
    ret = InsertIdentityAsNeeded(node);
    if (ret != SUCCESS) {
      GELOGE(ret, "Fail to insert Identity node.");
      return ret;
    }
  }
  GE_DUMP(compute_graph, "AfterHandleMemConflict");
  return SUCCESS;
}
}  // namespace ge
