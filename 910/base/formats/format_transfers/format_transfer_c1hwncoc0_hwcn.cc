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

#include "formats/format_transfers/format_transfer_c1hwncoc0_hwcn.h"

#include <securec.h>
#include <memory>

#include "formats/utils/formats_definitions.h"
#include "formats/utils/formats_trans_utils.h"
#include "framework/common/debug/ge_log.h"
#include "framework/common/debug/log.h"
#include "graph/utils/type_utils.h"

namespace ge {
namespace formats {
namespace {
bool CheckDataTypeSupportedForC1hwncoc0ToHwcn(const DataType &data_type) {
  return ((data_type == DT_FLOAT) || (data_type == DT_FLOAT16) || (data_type == DT_INT8));
}

Status CheckArgsForC1hwncoc0ToHwcn(const TransArgs &args) {
  const auto src_shape = args.src_shape;
  const auto dst_shape = args.dst_shape;
  if ((args.src_primary_format != FORMAT_C1HWNCoC0) || (args.dst_primary_format != FORMAT_HWCN)) {
    const std::string error = "Dose not support trans format from " +
        FmtToStr(TypeUtils::FormatToSerialString(args.src_primary_format)) + " to " +
        FmtToStr(TypeUtils::FormatToSerialString(args.dst_primary_format));
    GE_ERRORLOG_AND_ERRORMSG(ACL_ERROR_GE_FORMAT_INVALID, error.c_str());
    return ACL_ERROR_GE_FORMAT_INVALID;
  }
  if (!CheckDataTypeSupportedForC1hwncoc0ToHwcn(args.src_data_type)) {
    const std::string error = "Failed to trans shape from NC1HWNCoC0 to HWCN, invalid data type" +
        FmtToStr(TypeUtils::DataTypeToSerialString(args.src_data_type));
    GE_ERRORLOG_AND_ERRORMSG(ACL_ERROR_GE_DATATYPE_INVALID, error.c_str());
    return ACL_ERROR_GE_DATATYPE_INVALID;
  }
  if (!CheckShapeValid(src_shape, kC1hwncoc0DimsNum)) {
    GELOGE(ACL_ERROR_GE_SHAPE_INVALID, "[Check][SrcShape]Failed, src shape %s",
           ShapeToString(src_shape).c_str());
    REPORT_CALL_ERROR("E19999", "Failed to check src shape %s", ShapeToString(src_shape).c_str());
    return ACL_ERROR_GE_SHAPE_INVALID;
  }
  if (!CheckShapeValid(dst_shape, kHwcnDimsNum)) {
    GELOGE(ACL_ERROR_GE_SHAPE_INVALID, "[Check][DSTShape]Failed, dst shape %s.",
           ShapeToString(dst_shape).c_str());
    REPORT_CALL_ERROR("E19999", "Failed to check dst shape %s", ShapeToString(dst_shape).c_str());
    return ACL_ERROR_GE_SHAPE_INVALID;
  }
  const auto cube_size = GetC0Value(static_cast<int32_t>(args.src_format));
  if ((src_shape.at(kC1hwncoc0C1) != (((dst_shape.at(kHwcnC) - 1) / cube_size) + 1)) ||
      (src_shape.at(kC1hwncoc0H) != dst_shape.at(kHwcnH)) || (src_shape.at(kC1hwncoc0W) != dst_shape.at(kHwcnW)) ||
      (src_shape.at(kC1hwncoc0N) != dst_shape.at(kHwcnN)) || (src_shape.at(kC1hwncoc0Co) != cube_size) ||
      (src_shape.at(kC1hwncoc0C0) != cube_size)) {
    const std::string error = "Failed to check relationship between src and dst shape, src shape" +
        FmtToStr(ShapeToString(src_shape)) + ", dst shape" + FmtToStr(ShapeToString(dst_shape));
    GE_ERRORLOG_AND_ERRORMSG(ACL_ERROR_GE_SHAPE_INVALID, error.c_str());
    return ACL_ERROR_GE_SHAPE_INVALID;
  }

  return SUCCESS;
}

Status GetDstDataAfterTransForC1hwncoc0ToHwcn(const TransArgs &args, TransResult &result,
                                              const int32_t size, const int64_t total_size) {
  const std::shared_ptr<uint8_t> dst(new (std::nothrow) uint8_t[total_size], std::default_delete<uint8_t[]>());
  if (dst == nullptr) {
    GELOGE(ACL_ERROR_GE_MEMORY_ALLOCATION,
           "[Allocate][DSTMemory]Failed to allcoate memory for dst buf %" PRId64 ", "
           "shape %s when trans format from %s to %s", total_size, ShapeToString(args.dst_shape).c_str(),
           TypeUtils::FormatToSerialString(args.src_format).c_str(),
           TypeUtils::FormatToSerialString(args.dst_format).c_str());
    REPORT_CALL_ERROR("E19999", "Failed to allcoate memory for dst buf %" PRId64 ", "
                      "shape %s when trans format from %s to %s", total_size, ShapeToString(args.dst_shape).c_str(),
                      TypeUtils::FormatToSerialString(args.src_format).c_str(),
                      TypeUtils::FormatToSerialString(args.dst_format).c_str());
    return ACL_ERROR_GE_MEMORY_ALLOCATION;
  }

  const auto h = args.src_shape.at(kC1hwncoc0H);
  const auto w = args.src_shape.at(kC1hwncoc0W);
  const auto n = args.src_shape.at(kC1hwncoc0N);
  const auto c0 = args.src_shape.at(kC1hwncoc0C0);
  const auto co = args.src_shape.at(kC1hwncoc0Co);
  const auto c = args.dst_shape.at(kHwcnC);
  const auto cube_size = GetC0Value(static_cast<int32_t>(args.src_format));
  const int64_t cn = c * n;
  const int64_t wcn = w * cn;
  const int64_t coc0 = co * c0;
  const int64_t ncoc0 = n * coc0;
  const int64_t wncoc0 = w * ncoc0;

  for (int64_t h_idx = 0; h_idx < h; h_idx++) {
    const int64_t h_head_addr = h_idx * wcn;
    for (int64_t w_idx = 0; w_idx < w; w_idx++) {
      const int64_t w_head_addr = h_head_addr + (w_idx * cn);
      for (int64_t c_idx = 0; c_idx < c; c_idx++) {
        const int64_t c_head_addr = w_head_addr + (c_idx * n);
        for (int64_t n_idx = 0; n_idx < n; n_idx++) {
          const int64_t dst_idx = c_head_addr + n_idx;
          const int64_t c1_idx = c_idx / cube_size;
          const int64_t c0_idx = c_idx % cube_size;
          const int64_t co_idx = c0_idx;
          const int64_t hwncoc0 = h * wncoc0;
          const int64_t src_idx =
              (c1_idx * hwncoc0) + (h_idx * wncoc0) + (w_idx * ncoc0) + (n_idx * coc0) + (co_idx * c0) + c0_idx;
          const auto src_offset = src_idx * size;
          const auto dst_offset = dst_idx * size;
          // The memcpy_s/memset_s argument `dstMax` must be less than 2G
          const auto protected_size = ((total_size - dst_offset) < static_cast<int64_t>(SECUREC_MEM_MAX_LEN))
                                         ? (total_size - dst_offset)
                                         : static_cast<int64_t>(SECUREC_MEM_MAX_LEN);
          GE_CHECK_GE(protected_size, 0);
          const auto ret = memcpy_s(dst.get() + dst_offset, static_cast<size_t>(protected_size), args.data + src_offset,
                                    static_cast<size_t>(size));
          if (ret != EOK) {
            GELOGE(ACL_ERROR_GE_MEMORY_OPERATE_FAILED,
                   "[Operate][Memory]Failed to copy data from "
                   "C1HWNCoC0[%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", "
                   "%" PRId64 ", %" PRId64 "] offset %" PRId64 " to "
                   "HWCN[%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "] offset %" PRId64 ", err-code %d",
                   c1_idx, h_idx, w_idx, n_idx, co_idx, c0_idx, src_offset, h_idx, w_idx, c_idx, n_idx,
                   dst_offset, ret);
            REPORT_CALL_ERROR("E19999", "Failed to copy data from C1HWNCoC0[%" PRId64 ", %" PRId64 ", "
                              "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "] offset %" PRId64 " to "
                              "HWCN[%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "] offset %" PRId64 ", "
                              "err-code %d", c1_idx, h_idx, w_idx, n_idx, co_idx, c0_idx, src_offset,
                              h_idx, w_idx, c_idx, n_idx, dst_offset, ret);
            return ACL_ERROR_GE_MEMORY_OPERATE_FAILED;
          }
        }
      }
    }
  }
  result.data = dst;
  result.length = static_cast<size_t>(total_size);
  return SUCCESS;
}
}  // namespace

Status FormatTransferC1hwncoc0Hwcn::TransFormat(const TransArgs &args, TransResult &result) {
  Status ret = CheckArgsForC1hwncoc0ToHwcn(args);
  if (ret != SUCCESS) {
    return ret;
  }
  const int32_t size = GetSizeByDataType(args.src_data_type);
  const int64_t total_size = GetItemNumByShape(args.dst_shape) * size;
  if (total_size <= 0) {
    const int64_t src_size = GetItemNumByShape(args.src_shape);
    if ((total_size == 0) && (src_size == 0)) {
      result.length = static_cast<size_t>(total_size);
      return SUCCESS;
    }
    GELOGE(ACL_ERROR_GE_SHAPE_INVALID, "[Get][Shape]Failed, total size %" PRId64 " from dst shape %s, "
           "src shape %s.",
           total_size, ShapeToString(args.dst_shape).c_str(),
           ShapeToString(args.src_shape).c_str());
    REPORT_CALL_ERROR("E19999", "Get shape faield, total size %" PRId64 " from dst shape %s, src shape %s.",
                      total_size, ShapeToString(args.dst_shape).c_str(),
                      ShapeToString(args.src_shape).c_str());
    return ACL_ERROR_GE_SHAPE_INVALID;
  }
  GELOGD("Begin to trans format from C1HWNCoC0 to HWCN, src shape %s, data type %s, dst shape %s, "
         "memory size %" PRId64 ".",
         ShapeToString(args.src_shape).c_str(), TypeUtils::DataTypeToSerialString(args.src_data_type).c_str(),
         ShapeToString(args.dst_shape).c_str(), total_size);
  ret = GetDstDataAfterTransForC1hwncoc0ToHwcn(args, result, size, total_size);
  if (ret != SUCCESS) {
    GELOGE(ret, "[Get][Data]Failed when after trans, src shape %s, data type %s, dst shape %s, "
           "memory size %" PRId64 ", error_code %u",
           ShapeToString(args.src_shape).c_str(),
           TypeUtils::DataTypeToSerialString(args.src_data_type).c_str(),
           ShapeToString(args.dst_shape).c_str(), total_size, ret);
    REPORT_CALL_ERROR("E19999", "Failed to get data after trans, src shape %s, data type %s, "
                      "dst shape %s, memory size %" PRId64 ", error_code %u",
                      ShapeToString(args.src_shape).c_str(),
                      TypeUtils::DataTypeToSerialString(args.src_data_type).c_str(),
                      ShapeToString(args.dst_shape).c_str(), total_size, ret);
    return ret;
  }
  return SUCCESS;
}

Status FormatTransferC1hwncoc0Hwcn::TransShape(const Format src_format, const std::vector<int64_t> &src_shape,
                                               const DataType data_type, const Format dst_format,
                                               std::vector<int64_t> &dst_shape) {
  (void)src_format;
  (void)src_shape;
  (void)data_type;
  (void)dst_format;
  (void)dst_shape;
  GELOGD("The shape derivation from C1HWNCoC0 to HWCN is not unique. Trans shape in this direction is not supported.");
  return ACL_ERROR_GE_FORMAT_INVALID;
}

REGISTER_FORMAT_TRANSFER(FormatTransferC1hwncoc0Hwcn, FORMAT_C1HWNCoC0, FORMAT_HWCN)
}  // namespace formats
}  // namespace ge
