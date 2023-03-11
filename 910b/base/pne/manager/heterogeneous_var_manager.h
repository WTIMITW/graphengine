/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All rights reserved.
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

#ifndef AIR_BASE_PNE_MANAGER_HETEROGENEOUS_VAR_MANAGER_H_
#define AIR_BASE_PNE_MANAGER_HETEROGENEOUS_VAR_MANAGER_H_

#include <map>
#include <memory>
#include <mutex>
#include "external/ge/ge_api_error_codes.h"
#include "graph/compute_graph.h"
#include "graph/manager/graph_manager_utils.h"
#include "graph/parallelism/tensor_parallel_attrs.h"
#include "pne/model/flow_model.h"

namespace ge {
class HeterogeneousVarManager {
 public:
  using LoadModelFunc = std::function<Status(const FlowModelPtr &, const GraphNodePtr &)>;
  using UnloadModelFunc = std::function<Status(const FlowModelPtr &, const uint32_t)>;
  using ExecModelFunc = std::function<Status(const GraphNodePtr &, const std::vector<GeTensor> &)>;
  struct DeploymentInfo {
    std::string node_deployment;
    std::string tensor_deployment;
  };

  static Status Initialize(const uint64_t session_id);
  static void Finalize(const uint64_t session_id);
  static std::shared_ptr<HeterogeneousVarManager> GetInstance(const uint64_t session_id);

  HeterogeneousVarManager() = default;
  ~HeterogeneousVarManager() = default;

  void SetInitGraphNode(const GraphNodePtr &graph_node);
  const std::map<uint32_t, GraphNodePtr> &GetInitGraphNodes() const;
  bool IsSuspended(const uint32_t graph_id) const;

  const DeploymentInfo *GetVarDeployment(const std::string &var_name) const;
  void UpdateVarDeployments(const std::map<std::string, DeploymentInfo> &var_deployments);
  Status RegisterInitModel(const FlowModelPtr &flow_model, const std::vector<size_t> &data_indices);

  Status RecordInitOp(const uint32_t graph_id, const std::vector<GeTensor> &inputs);
  Status LoadPendingModels(const LoadModelFunc &load_model_func);
  Status ExecutePendingInitOps(const ExecModelFunc &execute_model_func);
  Status UnloadGraph(const uint32_t graph_id, const UnloadModelFunc &load_model_func);

 private:
  struct VarState {
    int32_t state;
    DeploymentInfo deployment_info;
  };

  struct InitVarOperation {
    uint32_t graph_id;
    std::vector<GeTensor> inputs;
  };

  struct PartialModel {
    uint32_t model_id;
    FlowModelPtr flow_model;
    std::vector<size_t> input_indices;
  };

  static Status GetPartialModelInput(const PartialModel &partial_model,
                                     const std::vector<GeTensor> &inputs,
                                     std::vector<GeTensor> &partial_inputs);

  Status ExecutePendingInitOps(const uint32_t graph_id,
                               std::vector<PartialModel> &partial_models,
                               std::vector<InitVarOperation> &init_ops,
                               const ExecModelFunc &execute_model_func);

  std::map<std::string, VarState> var_deployments_;
  std::map<uint32_t, GraphNodePtr> graph_nodes_;
  std::map<uint32_t, std::vector<InitVarOperation>> pending_init_operations_;
  std::map<uint32_t, std::vector<PartialModel>> graph_id_to_partial_models_;

  static std::mutex mu_;
  static std::map<uint64_t, std::shared_ptr<HeterogeneousVarManager>> var_manager_map_;
};
}  // namespace ge

#endif  // AIR_BASE_PNE_MANAGER_HETEROGENEOUS_VAR_MANAGER_H_
