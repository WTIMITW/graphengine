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

#ifndef AIR_CXX_BASE_COMMON_GE_INNER_ATTRS_H_
#define AIR_CXX_BASE_COMMON_GE_INNER_ATTRS_H_
#include "external/graph/types.h"

namespace ge {
extern const char_t* kAttrNameSingleOpType;
// profiling name
extern const char_t *const kProfilingDeviceConfigData;
extern const char_t *const kProfilingIsExecuteOn;
// helper option
extern const char_t *const kHostMasterPidName;
extern const char_t *const kExecutorDevId;
// runtime 2.0
constexpr char const *kRequestWatcher = "_request_watcher";
constexpr char const *kWatcherAddress = "_watcher_address";
constexpr char const *kSubgraphInput = "_subgraph_input";
constexpr char const *kSubgraphOutput = "_subgraph_output";
constexpr char const *kKnownSubgraph = "_known_subgraph";
constexpr char const *kRelativeBranch = "branch";
constexpr char const *kConditionGraph = "CondGraph";
constexpr char const *kThenGraph = "then_graph";
constexpr char const *kElseGraph = "else_graph";
}
#endif // AIR_CXX_BASE_COMMON_GE_INNER_ATTRS_H_
