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

#ifndef GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_H_
#define GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/ge_types.h"
#include "common/helper/model_helper.h"
#include "common/helper/om_file_helper.h"
#include "common/opskernel/ge_task_info.h"
#include "common/properties_manager.h"
#include "common/types.h"
#include "framework/common/util.h"
#include "graph/debug/ge_attr_define.h"
#include "graph/load/new_model_manager/aipp_utils.h"
#include "graph/load/new_model_manager/data_dumper.h"
#include "graph/load/new_model_manager/data_inputer.h"
#include "graph/load/new_model_manager/model_utils.h"
#include "graph/load/new_model_manager/zero_copy_offset.h"
#include "graph/load/new_model_manager/zero_copy_task.h"
#include "graph/model.h"
#include "graph/node.h"
#include "graph/op_desc.h"
#include "graph/operator.h"
#include "graph/utils/attr_utils.h"
#include "graph/utils/tensor_utils.h"
#include "mmpa/mmpa_api.h"
#include "proto/task.pb.h"
#include "task_info/task_info.h"

namespace ge {
// op debug need 2048 bits buffer
const size_t kOpDebugMemorySize = 2048UL;
const size_t kDebugP2pSize = 8UL;

typedef enum tagModelProcStage {
  MODEL_LOAD_START = 1,
  MODEL_LOAD_END,
  MODEL_PRE_PROC_START,
  MODEL_PRE_PROC_END,
  MODEL_INFER_START,
  MODEL_INFER_END,
  MODEL_AFTER_PROC_START,
  MODEL_AFTER_PROC_END,
  MODEL_PROC_INVALID,
} ModelProcStage;

struct timeInfo {
  uint32_t modelId;
  int64_t processBeginTime;
  int64_t processEndTime;
  int64_t inferenceBeginTime;
  int64_t inferenceEndTime;
  int64_t dumpBeginTime;
  int64_t dumpEndTime;
};

// comments
class DavinciModel {
 public:
  ///
  /// @ingroup ge
  /// @brief DavinciModel constructor
  /// @author
  ///
  DavinciModel(int32_t priority, const std::shared_ptr<ModelListener> &listener);

  ///
  /// @ingroup ge
  /// @brief DavinciModel desctructor, free Parse and Init resources
  /// @author
  ///
  ~DavinciModel();

  ///
  /// @ingroup ge
  /// @brief apply model to model_def_
  ///
  Status Assign(const GeModelPtr &ge_model);

  ///
  /// @ingroup ge
  /// @brief DavinciModel initialization, including Stream, ccHandle, Event, DataInputer, etc
  /// @return execute result
  /// @author
  ///
  Status Init(void *dev_ptr = nullptr, size_t memsize = 0, void *weight_ptr = nullptr, size_t weightsize = 0);

  ///
  /// @ingroup ge
  /// @brief ACL case, Load task list with queue.
  /// @param [in] input_que_ids: input queue ids from user, nums equal Data Op.
  /// @param [in] output_que_ids: input queue ids from user, nums equal NetOutput Op.
  /// @return: 0 for success / others for fail
  ///
  Status SetQueIds(const std::vector<uint32_t> &input_queue_ids, const std::vector<uint32_t> &output_queue_ids);

  ///
  /// @ingroup ge
  /// @brief Get DataInputer
  /// @return model ID
  ///
  uint32_t Id() const { return model_id_; }

  ///
  /// @ingroup ge
  /// @brief Get DataInputer
  /// @return model ID
  ///
  void SetId(uint32_t model_id) { model_id_ = model_id; }

  static void *Run(DavinciModel *model_pointer);

  ///
  /// @ingroup ge
  /// @brief NnExecute
  /// @param [in] stream   execute stream
  /// @param [in] async_mode  is asynchronize mode.
  /// @param [in] input_data  model input data
  /// @param [out] output_data  model output data
  ///
  Status NnExecute(rtStream_t stream, bool async_mode, const InputData &input_data, OutputData &output_data);

  ///
  /// @ingroup ge
  /// @brief lock mutex run flag
  /// @author
  ///
  void LockRunFlg() { mux_run_flg_.lock(); }

  ///
  /// @ingroup ge
  /// @brief unlock mutex run flag
  /// @author
  ///
  void UnlockRunFlg() { mux_run_flg_.unlock(); }

  ///
  /// @ingroup ge
  /// @brief get DataInputer
  /// @return DataInputer pointer
  ///
  DataInputer *const GetDataInputer() const { return data_inputer_; }

  // get Stream number
  uint32_t StreamNum() const { return runtime_param_.stream_num; }

  // get Event number
  uint32_t EventNum() const { return runtime_param_.event_num; }

  // get Lable number
  uint32_t LabelNum() const { return runtime_param_.label_num; }

  // get batch number
  uint32_t BatchNum() const { return runtime_param_.batch_num; }

  // get session id
  uint64_t SessionId() const { return runtime_param_.session_id; }

  // get model priority
  int32_t Priority() const { return priority_; }

  // get total mem size
  size_t TotalMemSize() const { return runtime_param_.mem_size; }

  // model name
  string Name() const { return name_; }

  // om_name
  string OmName() const { return om_name_; }
  // version
  uint32_t Version() const { return version_; }

  // get total weights mem size
  size_t TotalWeightsMemSize() const { return runtime_param_.weight_size; }

  size_t TotalVarMemSize() const { return runtime_param_.var_size; }

  // get base memory address
  uint8_t *MemBase() { return mem_base_; }

  // get weight base memory address
  uint8_t *WeightsMemBase() { return weights_mem_base_; }

  uint8_t *VarMemBase() { return var_mem_base_; }

  // get Event list
  const vector<rtEvent_t> &GetEventList() const { return event_list_; }

  const vector<rtStream_t> &GetStreamList() const { return stream_list_; }

  const vector<rtLabel_t> &GetLabelList() const { return label_list_; }

  Status DestroyThread();

  // Get Data Op.
  const vector<OpDescPtr> &GetDataList() const { return data_op_list_; }

  // get Op
  const map<uint32_t, OpDescPtr> &GetOpList() const { return op_list_; }

  OpDescPtr GetOpByIndex(uint32_t index) const {
    if (op_list_.find(index) == op_list_.end()) {
      return nullptr;
    }
    return op_list_.at(index);
  }

  OpDescPtr GetVariableOp(const string &name) {
    for (auto op_desc : variable_op_list_) {
      if (op_desc != nullptr && op_desc->GetName() == name) {
        return op_desc;
      }
    }
    return nullptr;
  }
  // get task info for profiling
  const std::vector<TaskDescInfo> &GetTaskDescInfo() const { return task_desc_info_; }

  // get updated task info list
  std::vector<TaskInfoPtr> GetTaskList() { return task_list_; }

  ///
  /// @ingroup ge
  /// @brief get model input and output format
  /// @return ccTensorFormat_t current model input and output format
  ///
  Format GetFormat();

  rtModel_t GetRtModelHandle() const { return rt_model_handle_; }

  rtStream_t GetRtModelStream() const { return rt_model_stream_; }

  uint64_t GetRtBaseAddr() const { return runtime_param_.logic_mem_base; }

  uint64_t GetRtWeightAddr() const { return runtime_param_.logic_weight_base; }

  uint64_t GetRtVarAddr() const { return runtime_param_.logic_var_base; }

  uint32_t GetFlowctrlIndex(uint32_t op_index);

  void PushHcclStream(rtStream_t value);

  bool IsBroadCastOpData(const NodePtr &var_node);

  ///
  /// @ingroup ge
  /// @brief For TVM Op, avoid Addr Reuse.
  /// @return void*
  ///
  const char *GetRegisterStub(const string &tvm_binfile_key, const string &session_graph_model_id = "");

  ///
  /// @ingroup ge
  /// @brief get model input and output desc info
  /// @param [out] input_shape  model input size
  /// @param [out] output_shape model output size
  /// @return execute result
  ///
  Status GetInputOutputDescInfo(vector<InputOutputDescInfo> &input_desc, vector<InputOutputDescInfo> &output_desc);

  Status GetInputOutputDescInfo(vector<InputOutputDescInfo> &input_desc, vector<InputOutputDescInfo> &output_desc,
                                std::vector<uint32_t> &inputFormats, std::vector<uint32_t> &output_formats);

  ///
  /// @ingroup ge
  /// @brief Get dynamic batch_info
  /// @param [out] batch_info
  /// @param [out] dynamic_type
  /// @return execute result
  ///
  Status GetDynamicBatchInfo(std::vector<std::vector<int64_t>> &batch_info, int32_t &dynamic_type) const;

  ///
  /// @ingroup ge
  /// @brief Get combined dynamic dims info
  /// @param [out] batch_info
  /// @return None
  ///
  void GetCombinedDynamicDims(std::vector<std::vector<int64_t>> &batch_info) const;

  void GetUserDesignateShapeOrder(std::vector<std::string> &user_input_shape_order) const;

  void GetCurShape(std::vector<int64_t> &batch_info, int32_t &dynamic_type);

  void GetModelAttr(std::vector<std::string> &dynamic_output_shape_info);

  ///
  /// @ingroup ge
  /// @brief Get AIPP input info
  /// @param [in] index
  /// @param [out] aipp_info
  /// @return execute result
  ///
  Status GetAIPPInfo(uint32_t index, AippConfigInfo &aipp_info);

  ///
  /// @ingroup ge
  /// @brief Get model_id.
  /// @return model_id
  ///
  uint32_t GetModelId() const { return model_id_; }

  ///
  /// @ingroup ge
  /// @brief get unique identification for op when load two or more models
  /// @param [in] op_desc : current op.
  /// @param [in] string identification: unique identification for current op.
  /// @return None
  ///
  void GetUniqueId(const OpDescPtr &op_desc, std::string &unique_identification);

  ///
  /// @ingroup ge
  /// @brief get model input and output desc for zero copy
  /// @param [out] input_shape  model input size
  /// @param [out] output_shape model output size
  /// @return execute result
  ///
  Status GetInputOutputDescInfoForZeroCopy(vector<InputOutputDescInfo> &input_desc,
                                           vector<InputOutputDescInfo> &output_desc,
                                           std::vector<uint32_t> &inputFormats, std::vector<uint32_t> &output_formats);

  Status ReturnResult(uint32_t data_id, const bool rslt_flg, const bool seq_end_flg, OutputData *output_data);

  Status ReturnNoOutput(uint32_t data_id);

  ///
  /// @ingroup ge
  /// @brief dump all op input and output information
  /// @return void
  ///
  void DumpOpInputOutput();

  ///
  /// @ingroup ge
  /// @brief dump single op input and output information
  /// @param [in] dump_op model_id
  /// @return Status
  ///
  Status DumpSingleOpInputOutput(const OpDescPtr &dump_op);

  Status ModelRunStart();

  ///
  /// @ingroup ge
  /// @brief stop run model
  /// @return Status
  ///
  Status ModelRunStop();

  ///
  /// @ingroup ge
  /// @brief model run flag
  /// @return Status
  ///
  bool RunFlag() const { return run_flg_; }

  Status GetOutputDescInfo(vector<InputOutputDescInfo> &output_desc, std::vector<uint32_t> &formats);

  ///
  /// @ingroup ge
  /// @brief Set Session Id
  /// @return void
  ///
  void SetSessionId(uint64_t session_id) { session_id_ = session_id; }

  ///
  /// @ingroup ge
  /// @brief Get Session Id
  /// @return sessionID
  ///
  uint64_t GetSessionId() const { return session_id_; }

  ///
  /// @ingroup ge
  /// @brief SetDeviceId
  /// @return void
  ///
  void SetDeviceId(uint32_t device_id) { device_id_ = device_id; }

  ///
  /// @ingroup ge
  /// @brief Get device Id
  /// @return  device id
  ///
  uint32_t GetDeviceId() const { return device_id_; }

  bool NeedDestroyAicpuKernel() const { return need_destroy_aicpu_kernel_; }

  Status UpdateSessionId(uint64_t session_id);

  const RuntimeParam &GetRuntimeParam() { return runtime_param_; }

  int32_t GetDataInputTid() const { return dataInputTid; }
  void SetDataInputTid(int32_t data_input_tid) { dataInputTid = data_input_tid; }

  void DisableZeroCopy(const void *addr);

  ///
  /// @ingroup ge
  /// @brief Save outside address of Data or NetOutput used info for ZeroCopy.
  /// @param [in] const OpDescPtr &op_desc: current op desc
  /// @param [in] const std::vector<void *> &outside_addrs: address of task
  /// @param [in] const void *args_offset: arguments address save the address.
  /// @return None.
  ///
  void SetZeroCopyAddr(const OpDescPtr &op_desc, const std::vector<void *> &outside_addrs, const void *info, void *args,
                       size_t size, size_t offset);

  void SetDynamicSize(const std::vector<uint64_t> &batch_num, int32_t dynamic_type);

  bool GetL1FusionEnableOption() { return is_l1_fusion_enable_; }

  void SetProfileTime(ModelProcStage stage, int64_t endTime = 0);

  int64_t GetLoadBeginTime() { return load_begin_time_; }

  int64_t GetLoadEndTime() { return load_end_time_; }

  Status SinkModelProfile();

  Status SinkTimeProfile(const InputData &current_data);

  void SaveDumpOpInfo(const RuntimeParam &model_param, const OpDescPtr &op, uint32_t task_id, uint32_t stream_id) {
    data_dumper_.SaveDumpOpInfo(model_param, op, task_id, stream_id);
  }

  void SaveDumpTask(uint32_t task_id, uint32_t stream_id, const std::shared_ptr<OpDesc> &op_desc, uintptr_t args) {
    data_dumper_.SaveDumpTask(task_id, stream_id, op_desc, args);
  }

  void SetEndGraphId(uint32_t task_id, uint32_t stream_id);
  DavinciModel &operator=(const DavinciModel &model) = delete;

  DavinciModel(const DavinciModel &model) = delete;

  const map<int64_t, std::vector<rtStream_t>> &GetHcclFolowStream() { return main_follow_stream_mapping_; }
  void SaveHcclFollowStream(int64_t main_stream_id, rtStream_t stream);

  void InitRuntimeParams();
  Status InitVariableMem();

  void UpdateMemBase(uint8_t *mem_base) {
    runtime_param_.mem_base = mem_base;
    mem_base_ = mem_base;
  }
  void SetTotalArgsSize(uint32_t args_size) { total_args_size_ += args_size; }
  uint32_t GetTotalArgsSize() { return total_args_size_; }
  void *GetCurrentArgsAddr(uint32_t offset) {
    void *cur_args = static_cast<char *>(args_) + offset;
    return cur_args;
  }
  void SetTotalIOAddrs(vector<void *> &io_addrs) {
    total_io_addrs_.insert(total_io_addrs_.end(), io_addrs.begin(), io_addrs.end());
  }
  void SetTotalFixedAddrsSize(string tensor_name, int64_t fix_addr_size);
  int64_t GetFixedAddrsSize(string tensor_name);
  void *GetCurrentFixedAddr(int64_t offset) const {
    void *cur_addr = static_cast<char *>(fixed_addrs_) + offset;
    return cur_addr;
  }

  uint32_t GetFixedAddrOutputIndex(string tensor_name) {
    if (tensor_name_to_peer_output_index_.find(tensor_name) != tensor_name_to_peer_output_index_.end()) {
      return tensor_name_to_peer_output_index_[tensor_name];
    }
    return UINT32_MAX;
  }
  void SetKnownNode(bool known_node) { known_node_ = known_node; }
  bool IsKnownNode() { return known_node_; }
  Status MallocKnownArgs();
  Status UpdateKnownNodeArgs(const vector<void *> &inputs, const vector<void *> &outputs);
  Status CreateKnownZeroCopyMap(const vector<void *> &inputs, const vector<void *> &outputs);
  Status UpdateKnownZeroCopyAddr();
  void SetKnownNodeAddrNotChanged(bool base_addr_not_changed) { base_addr_not_changed_ = base_addr_not_changed; }

  Status GetOrigInputInfo(uint32_t index, OriginInputInfo &orig_input_info);
  Status GetAllAippInputOutputDims(uint32_t index, std::vector<InputOutputDims> &input_dims,
                                   std::vector<InputOutputDims> &output_dims);
  void SetModelDescVersion(bool is_new_model_desc) { is_new_model_desc_ = is_new_model_desc; }
  // om file name
  void SetOmName(string om_name) { om_name_ = om_name; }

  void SetDumpProperties(const DumpProperties &dump_properties) { data_dumper_.SetDumpProperties(dump_properties); }
  const DumpProperties &GetDumpProperties() const { return data_dumper_.GetDumpProperties(); }

  void SetMemcpyOffsetAndAddr(map<int64_t, void *> &memcpy_4g_offset_addr) {
    memcpy_4g_offset_addr_.insert(memcpy_4g_offset_addr.begin(), memcpy_4g_offset_addr.end());
  }
  const map<int64_t, void *> &GetMemcpyOffsetAndAddr() const { return memcpy_4g_offset_addr_; }

  bool GetOpDescInfo(uint32_t stream_id, uint32_t task_id, OpDescInfo &op_desc_info) const {
    return data_dumper_.GetOpDescInfo(stream_id, task_id, op_desc_info);
  }
  Status InitInputOutputForDynamic(const ComputeGraphPtr &compute_graph);

 private:
  // memory address of weights
  uint8_t *weights_mem_base_;
  uint8_t *var_mem_base_;
  // memory address of model
  uint8_t *mem_base_;
  bool is_inner_mem_base_;
  bool is_inner_weight_base_;
  // input data manager
  DataInputer *data_inputer_;

  int64_t load_begin_time_;
  int64_t load_end_time_;
  struct timeInfo time_info_;
  int32_t dataInputTid;

  ///
  /// @ingroup ge
  /// @brief Save Batch label Info.
  /// @param [in] const OpDescPtr &op_desc
  /// @param [in] uintptr_t addr: address value in args block.
  /// @return None.
  ///
  void SetBatchLabelAddr(const OpDescPtr &op_desc, uintptr_t addr);

  ///
  /// @ingroup ge
  /// @brief Copy Check input size and model op size.
  /// @param [in] const int64_t &input_size: input size.
  /// @param [in] const int64_t &op_size: model op size.
  /// @param [in] is_dynamic: dynamic batch input flag.
  /// @return true if success
  ///
  bool CheckInputAndModelSize(const int64_t &input_size, const int64_t &op_size, bool is_dynamic);

  ///
  /// @ingroup ge
  /// @brief Set copy only for No task feed NetOutput address.
  /// @return None.
  ///
  void SetCopyOnlyOutput();

  ///
  /// @ingroup ge
  /// @brief Copy Input/Output to model for direct use.
  /// @param [in] const InputData &input_data: user input data info.
  /// @param [in/out] OutputData &output_data: user output data info.
  /// @param [in] bool is_dynamic: whether is dynamic input, true: is dynamic input; false: not is dynamic input
  /// @return SUCCESS handle successfully / others handle failed
  ///
  Status CopyModelData(const InputData &input_data, OutputData &output_data, bool is_dynamic);

  ///
  /// @ingroup ge
  /// @brief Copy Data addr to model for direct use.
  /// @param [in] data_info: model memory addr/size map { data_index, { tensor_size, tensor_addr } }.
  /// @param [in] is_input: input data or output data
  /// @param [in] blobs: user input/output data list.
  /// @param [in] is_dynamic: whether is dynamic input, true: is dynamic input; false: not is dynamic input
  /// @param [in] batch_label: batch label for multi-batch scenes
  /// @return SUCCESS handle successfully / others handle failed
  ///
  Status UpdateIoTaskArgs(const std::map<uint32_t, ZeroCopyOffset> &data_info, bool is_input,
                          const vector<DataBuffer> &blobs, bool is_dynamic, const string &batch_label);

  Status CopyInputData(const InputData &input_data, bool device_data = false);

  Status CopyOutputData(uint32_t data_id, OutputData &output_data, rtMemcpyKind_t kind);

  Status SyncVarData();

  Status InitModelMem(void *dev_ptr, size_t memsize, void *weight_ptr, size_t weightsize);

  void CreateInputDimsInfo(const OpDescPtr &op_desc, Format format, InputOutputDescInfo &input);

  void SetInputDimsInfo(const vector<int64_t> &model_input_dims, Format &format, InputOutputDescInfo &input);

  Status GetInputDescInfo(vector<InputOutputDescInfo> &input_desc, std::vector<uint32_t> &formats);

  Status InitTaskInfo(domi::ModelTaskDef &modelTaskInfo);

  void UnbindHcomStream();

  Status DistributeTask();

  uint8_t *MallocFeatureMapMem(size_t data_size);

  uint8_t *MallocWeightsMem(size_t weights_size);

  void FreeFeatureMapMem();

  void FreeWeightsMem();

  void ReleaseTask();

  void UnbindTaskSinkStream();

  bool IsAicpuKernelConnectSpecifiedLayer();

  ///
  /// @ingroup ge
  /// @brief Reduce memory usage after task sink.
  /// @return: void
  ///
  void Shrink();

  ///
  /// @ingroup ge
  /// @brief Travel all nodes and do some init.
  /// @param [in] compute_graph: ComputeGraph to load.
  /// @return Status
  ///
  Status InitNodes(const ComputeGraphPtr &compute_graph);

  ///
  /// @ingroup ge
  /// @brief Data Op Initialize.
  /// @param [in] NodePtr: Data Op.
  /// @param [in/out] data_op_index: NetOutput addr size info.
  /// @return Status
  ///
  Status InitDataOp(const NodePtr &node, uint32_t &data_op_index, map<uint32_t, OpDescPtr> &data_by_index);

  ///
  /// @ingroup ge
  /// @brief Sort Data op list by index.
  /// @param [in] data_by_index: map of Data Op.
  /// @return
  ///
  void AdjustDataOpList(const map<uint32_t, OpDescPtr> &data_by_index);

  ///
  /// @ingroup ge
  /// @brief input zero copy node Initialize.
  /// @param [in] NodePtr: Data Op.
  /// @return Status
  ///
  Status InitInputZeroCopyNodes(const NodePtr &node);

  ///
  /// @ingroup ge
  /// @brief NetOutput Op Initialize.
  /// @param [in] NodePtr: NetOutput Op.
  /// @return Status
  ///
  Status InitNetOutput(const NodePtr &node);

  ///
  /// @ingroup ge
  /// @brief output zero copy node Initialize.
  /// @param [in] NodePtr: Data Op.
  /// @return Status
  ///
  Status InitOutputZeroCopyNodes(const NodePtr &node);

  ///
  /// @ingroup ge
  /// @brief Constant Op Init.
  /// @return Status
  ///
  Status InitConstant(const OpDescPtr &op_desc);

  Status InitVariable(const OpDescPtr &op_desc);

  /// @ingroup ge
  /// @brief LabelSet Op Initialize.
  /// @param [in] op_desc: LabelSet Op descriptor.
  /// @return Status
  Status InitLabelSet(const OpDescPtr &op_desc);

  Status InitStreamSwitch(const OpDescPtr &op_desc);

  Status InitStreamActive(const OpDescPtr &op_desc);

  Status InitStreamSwitchN(const OpDescPtr &op_desc);

  ///
  /// @ingroup ge
  /// @brief Case Op Init.
  /// @return Status
  ///
  Status InitCase(const OpDescPtr &op_desc);

  Status SetDynamicBatchInfo(const OpDescPtr &op_desc, uint32_t batch_num);

  ///
  /// @ingroup ge
  /// @brief TVM Op Init.
  /// @return Status
  ///
  Status InitTbeHandle(const OpDescPtr &op_desc);

  void StoreTbeHandle(const std::string &handle_key);
  void CleanTbeHandle();

  ///
  /// @ingroup ge
  /// @brief Make active stream list and bind to model.
  /// @return: 0 for success / others for fail
  ///
  Status BindModelStream();

  ///
  /// @ingroup ge
  /// @brief Init model stream for NN model.
  /// @return Status
  ///
  Status InitModelStream(rtStream_t stream);

  ///
  /// @ingroup ge
  /// @brief ACL, Load task list with queue entrance.
  /// @return: 0 for success / others for fail
  ///
  Status LoadWithQueue();

  ///
  /// @ingroup ge
  /// @brief ACL, Bind Data Op addr to input queue.
  /// @return: 0 for success / others for fail
  ///
  Status BindInputQueue();

  Status CpuTaskModelZeroCopy(std::vector<uintptr_t> &mbuf_list, std::map<const void *, ZeroCopyOffset> &outside_addrs);

  ///
  /// @ingroup ge
  /// @brief ACL, Bind NetOutput Op addr to output queue.
  /// @return: 0 for success / others for fail
  ///
  Status BindOutputQueue();
  Status CpuModelPrepareOutput(uintptr_t addr, uint32_t size);

  ///
  /// @ingroup ge
  /// @brief definiteness queue schedule, bind input queue to task.
  /// @param [in] queue_id: input queue id from user.
  /// @param [in] addr: Data Op output tensor address.
  /// @param [in] size: Data Op output tensor size.
  /// @return: 0 for success / others for fail
  ///
  Status CpuModelDequeue(uint32_t queue_id);

  ///
  /// @ingroup ge
  /// @brief definiteness queue schedule, bind output queue to task.
  /// @param [in] queue_id: output queue id from user.
  /// @param [in] addr: NetOutput Op input tensor address.
  /// @param [in] size: NetOutput Op input tensor size.
  /// @return: 0 for success / others for fail
  ///
  Status CpuModelEnqueue(uint32_t queue_id, uintptr_t addr, uint32_t size);

  ///
  /// @ingroup ge
  /// @brief definiteness queue schedule, active original model stream.
  /// @return: 0 for success / others for fail
  ///
  Status CpuActiveStream();

  ///
  /// @ingroup ge
  /// @brief definiteness queue schedule, wait for end graph.
  /// @return: 0 for success / others for fail
  ///
  Status CpuWaitEndGraph();

  Status BindEnqueue();
  Status CpuModelEnqueue(uint32_t queue_id, uintptr_t out_mbuf);
  ///
  /// @ingroup ge
  /// @brief definiteness queue schedule, repeat run model.
  /// @return: 0 for success / others for fail
  ///
  Status CpuModelRepeat();

  Status InitEntryTask();
  Status AddHeadStream();

  ///
  /// @ingroup ge
  /// @brief set ts device.
  /// @return: 0 for success / others for fail
  ///
  Status SetTSDevice();

  Status OpDebugRegister();

  void OpDebugUnRegister();

  void CheckHasHcomOp();

  Status DoTaskSink();

  void CreateOutput(uint32_t index, OpDescPtr &op_desc, InputOutputDescInfo &output, uint32_t &format_result);

  Status TransAllVarData(ComputeGraphPtr &graph, uint32_t graph_id);

  // get desc info of graph for profiling
  Status GetComputeGraphInfo(const ComputeGraphPtr &graph, vector<ComputeGraphDescInfo> &graph_desc_info);

  void SetDataDumperArgs(const ComputeGraphPtr &compute_graph);

  Status GenOutputTensorInfo(const OpDescPtr &op_desc, uint32_t data_index, OutputData *output_data,
                             std::vector<ge::OutputTensorInfo> &outputs);

  void ParseAIPPInfo(std::string in_out_info, InputOutputDims &dims_info);
  void GetFixedAddrAttr(const OpDescPtr &op_desc);

  bool is_model_has_inited_;
  uint32_t model_id_;
  uint32_t runtime_model_id_;
  string name_;

  // used for inference data dump
  string om_name_;

  uint32_t version_;
  GeModelPtr ge_model_;

  bool need_destroy_aicpu_kernel_{false};
  vector<std::string> out_node_name_;

  map<uint32_t, OpDescPtr> op_list_;

  // data op_desc
  vector<OpDescPtr> data_op_list_;

  vector<OpDescPtr> output_op_list_;

  vector<OpDescPtr> variable_op_list_;

  std::map<uint32_t, ZeroCopyOffset> new_input_data_info_;
  std::map<uint32_t, ZeroCopyOffset> new_output_data_info_;
  std::map<const void *, ZeroCopyOffset> new_input_outside_addrs_;
  std::map<const void *, ZeroCopyOffset> new_output_outside_addrs_;

  std::vector<void *> real_virtual_addrs_;

  // output op: save cce op actual needed memory size
  vector<int64_t> output_memory_size_list_;

  std::thread thread_id_;

  std::shared_ptr<ModelListener> listener_;

  bool run_flg_;

  std::mutex mux_run_flg_;

  int32_t priority_;

  vector<rtStream_t> stream_list_;

  std::mutex all_hccl_stream_list_mutex_;
  vector<rtStream_t> all_hccl_stream_list_;

  // for reuse hccl_follow_stream
  std::mutex capacity_of_stream_mutex_;
  std::map<int64_t, std::vector<rtStream_t>> main_follow_stream_mapping_;

  vector<rtEvent_t> event_list_;

  vector<rtLabel_t> label_list_;
  set<uint32_t> label_id_indication_;

  std::mutex outside_addrs_mutex_;
  std::vector<ZeroCopyTask> zero_copy_tasks_;  // Task used Data or NetOutput addr.
  std::set<const void *> copy_only_addrs_;     // Address need copy to original place.

  // {op_id, batch_label}
  std::map<int64_t, std::string> zero_copy_op_id_batch_label_;
  // {batch_label, addrs}
  std::map<std::string, std::set<uintptr_t>> zero_copy_batch_label_addrs_;

  std::vector<TaskInfoPtr> task_list_;
  // rt_moodel_handle
  rtModel_t rt_model_handle_;

  rtStream_t rt_model_stream_;

  bool is_inner_model_stream_;

  bool is_async_mode_;  // For NN execute, Async mode use rtMemcpyAsync on rt_model_stream_.

  bool is_stream_list_bind_{false};
  bool is_pure_head_stream_{false};
  rtStream_t rt_head_stream_{nullptr};
  rtStream_t rt_entry_stream_{nullptr};
  rtAicpuDeployType_t deploy_type_{AICPU_DEPLOY_RESERVED};

  // ACL queue schedule, save queue ids for Init.
  std::vector<TaskInfoPtr> cpu_task_list_;
  std::vector<uint32_t> input_queue_ids_;    // input queue ids created by caller.
  std::vector<uint32_t> output_queue_ids_;   // output queue ids created by caller.
  std::vector<uintptr_t> input_mbuf_list_;   // input mbuf created by dequeue task.
  std::vector<uintptr_t> output_mbuf_list_;  // output mbuf created by dequeue task.

  uint64_t session_id_;

  uint32_t device_id_;

  std::mutex flowctrl_op_index_internal_map_mutex_;
  std::map<uint32_t, uint32_t> flowctrl_op_index_internal_map_;

  std::vector<rtStream_t> active_stream_list_;
  std::set<uint32_t> active_stream_indication_;

  std::set<uint32_t> hcom_streams_;
  RuntimeParam runtime_param_;

  static std::mutex tvm_bin_mutex_;
  std::set<std::string> tvm_bin_kernel_;

  std::map<std::string, uint32_t> used_tbe_handle_map_;

  // for profiling task and graph info
  std::map<uint32_t, std::string> op_name_map_;
  std::vector<TaskDescInfo> task_desc_info_;

  int64_t maxDumpOpNum_;
  // for data dump
  DataDumper data_dumper_;
  uint64_t iterator_count_;
  bool is_l1_fusion_enable_;
  std::map<OpDescPtr, void *> saved_task_addrs_;
  void *l1_fusion_addr_ = nullptr;

  bool known_node_ = false;
  uint32_t total_args_size_ = 0;
  void *args_ = nullptr;
  void *args_host_ = nullptr;
  void *fixed_addrs_ = nullptr;
  int64_t total_fixed_addr_size_ = 0;
  std::map<const void *, void *> knonw_input_data_info_;
  std::map<const void *, void *> knonw_output_data_info_;
  vector<void *> total_io_addrs_;
  vector<void *> orig_total_io_addrs_;
  bool base_addr_not_changed_ = false;

  vector<vector<int64_t>> batch_info_;
  std::vector<std::vector<int64_t>> combined_batch_info_;
  vector<string> user_designate_shape_order_;
  int32_t dynamic_type_ = 0;
  bool is_dynamic_ = false;

  vector<uint64_t> batch_size_;
  // key: input tensor name, generally rts op;
  // value: the fixed addr of input anchor, same as the peer output anchor addr of the peer op
  std::map<string, int64_t> tensor_name_to_fixed_addr_size_;

  // key: input tensor name, generally rts op; value: the peer output anchor of the peer op
  std::map<string, int64_t> tensor_name_to_peer_output_index_;
  // if model is first execute
  bool is_first_execute_;
  // for op debug
  std::mutex debug_reg_mutex_;
  bool is_op_debug_reg_ = false;
  void *op_debug_addr_ = nullptr;
  void *p2p_debug_addr_ = nullptr;
  bool is_new_model_desc_{false};

  std::map<int64_t, void *> memcpy_4g_offset_addr_;
};
}  // namespace ge
#endif  // GE_GRAPH_LOAD_NEW_MODEL_MANAGER_DAVINCI_MODEL_H_
