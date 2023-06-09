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
#ifndef BASE_RUNTIME_DEPLOY_MODEL_DEPLOYER_H_
#define BASE_RUNTIME_DEPLOY_MODEL_DEPLOYER_H_

#include <map>
#include <string>
#include <vector>
#include "pne/model/flow_model.h"
#include "common/plugin/ge_util.h"
#include "external/ge/ge_api_error_codes.h"

namespace ge {
struct DeployResult {
  uint32_t model_id;
  std::vector<uint32_t> input_queue_ids;
  std::vector<uint32_t> output_queue_ids;
  std::vector<uint32_t> control_input_queue_ids;
  std::vector<uint32_t> control_output_queue_ids;
  std::function<Status(void)> dev_stat_callback;
  size_t replica_num = 1U;
  std::string input_model_name;
};

class ModelDeployer {
 public:
  ModelDeployer() = default;
  GE_DELETE_ASSIGN_AND_COPY(ModelDeployer);
  virtual ~ModelDeployer() = default;

  /// Deploy model to devices
  /// @param model                models to deploy
  /// @param model_relation       relation among the models, can be nullptr iff models contains single model
  /// @param input_queue_ids      queue id of inputs
  /// @param output_queue_ids     queue id of outputs
  /// @param deploy_result        deploy result
  /// @return                     SUCCESS if deployed successfully, otherwise returns appropriate error code
  virtual Status DeployModel(const FlowModelPtr &flow_model,
                             const std::vector<uint32_t> &input_queue_ids,
                             const std::vector<uint32_t> &output_queue_ids,
                             DeployResult &deploy_result) = 0;

  /// Undeploy model
  /// @param model_id             id of the deployed model
  /// @return                     SUCCESS if undeployed successfully, otherwise returns appropriate error code
  virtual Status Undeploy(const uint32_t model_id) = 0;

  /// Get local node index in device mesh
  /// @return                     empty means not support
  virtual std::vector<int32_t> GetLocalNodeIndex() { return std::vector<int32_t>(); }
};
}  // namespace ge

#endif  // BASE_RUNTIME_DEPLOY_MODEL_DEPLOYER_H_
