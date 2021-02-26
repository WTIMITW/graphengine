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

#include <gtest/gtest.h>

#define private public
#define protected public
#include "generator/ge_generator.h"
#include "graph/utils/tensor_utils.h"

using namespace std;

namespace ge {
class UtestGeGenerator : public testing::Test {
 protected:
  void SetUp() {}

  void TearDown() {}
};

TEST_F(UtestGeGenerator, test_build_single_op_offline) {
  GeTensorDesc tensor_desc(GeShape(), FORMAT_NCHW, DT_FLOAT);
  TensorUtils::SetSize(tensor_desc, 512);

  shared_ptr<OpDesc> op_desc = make_shared<OpDesc>("Add", "add");
  EXPECT_EQ(op_desc->AddInputDesc(tensor_desc), GRAPH_SUCCESS);
  EXPECT_EQ(op_desc->AddInputDesc(tensor_desc), GRAPH_SUCCESS);
  EXPECT_EQ(op_desc->AddOutputDesc(tensor_desc), GRAPH_SUCCESS);

  GeTensor tensor(tensor_desc);
  const vector<GeTensor> inputs = { tensor, tensor };
  const vector<GeTensor> outputs = { tensor };

  // not Initialize, impl is null.
  GeGenerator generator;
  EXPECT_EQ(generator.BuildSingleOpModel(op_desc, inputs, outputs, "offline_"), PARAM_INVALID);

  // const map<string, string> &options
  generator.Initialize({});
  EXPECT_EQ(generator.BuildSingleOpModel(op_desc, inputs, outputs, "offline_"), GE_GENERATOR_GRAPH_MANAGER_BUILD_GRAPH_FAILED);
}

TEST_F(UtestGeGenerator, test_build_single_op_online) {
  GeTensorDesc tensor_desc(GeShape(), FORMAT_NCHW, DT_FLOAT);
  TensorUtils::SetSize(tensor_desc, 512);

  shared_ptr<OpDesc> op_desc = make_shared<OpDesc>("Add", "add");
  EXPECT_EQ(op_desc->AddInputDesc(tensor_desc), GRAPH_SUCCESS);
  EXPECT_EQ(op_desc->AddInputDesc(tensor_desc), GRAPH_SUCCESS);
  EXPECT_EQ(op_desc->AddOutputDesc(tensor_desc), GRAPH_SUCCESS);

  GeTensor tensor(tensor_desc);
  const vector<GeTensor> inputs = { tensor, tensor };
  const vector<GeTensor> outputs = { tensor };

  // not Initialize, impl is null.
  GeGenerator generator;
  generator.Initialize({});
  ModelBufferData model_buffer;
  EXPECT_EQ(generator.BuildSingleOpModel(op_desc, inputs, outputs, ENGINE_SYS, model_buffer), GE_GENERATOR_GRAPH_MANAGER_BUILD_GRAPH_FAILED);
}

}  // namespace ge