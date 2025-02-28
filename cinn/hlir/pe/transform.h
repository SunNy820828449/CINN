// Copyright (c) 2021 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include <absl/container/flat_hash_map.h>

#include <string>
#include <vector>

#include "cinn/ir/ir_base.h"
#include "cinn/ir/layout.h"
#include "cinn/poly/stage.h"

namespace cinn {
namespace hlir {
namespace pe {

/**
 * @brief basic PE that calculates a matrix multiplication
 *
 * @param A The first input tensor, [batch, M, K] or [M, K]
 * @param B The second input tensor, [batch, K, N] or [K, N]
 * @param trans_a whether A is transposed, default: false
 * @param trans_b whether B is transposed, default: false
 * @param alpha  The scale of output, default: 1.0.
 * @param name The name of the operation
 * @param target
 *
 * @return the output tensors
 */
std::vector<ir::Tensor> Matmul(const ir::Tensor& A,
                               const ir::Tensor& B,
                               bool trans_a            = false,
                               bool trans_b            = false,
                               float alpha             = 1,
                               const std::string& name = UniqName("T_Transform_Matmul_out"));

// realized by sharing buffer
ir::Tensor Reshape(const ir::Tensor& A,
                   const std::vector<int>& new_shape,
                   poly::StageMap stages,
                   const std::string& name);

ir::Tensor Reshape(const ir::Tensor& A,
                   const std::vector<int>& new_shape,
                   const std::string& name = UniqName("T_Transform_Matmul_out"));

ir::Tensor Concat(const ir::Tensor& A,
                  const ir::Tensor& B,
                  int axis                = 0,
                  const std::string& name = UniqName("T_Transform_Concat_out"));

std::vector<ir::Tensor> MatmulV2(const ir::Tensor& A,
                                 const ir::Tensor& B,
                                 bool trans_a                 = false,
                                 bool trans_b                 = false,
                                 float alpha                  = 1,
                                 const std::string& name      = UniqName("T_Transform_MatmulV2_out"),
                                 const common::Target& target = common::DefaultHostTarget());

std::vector<ir::Tensor> MatmulMKL(const ir::Tensor& A,
                                  const ir::Tensor& B,
                                  bool trans_a                 = false,
                                  bool trans_b                 = false,
                                  float alpha                  = 1,
                                  const std::string& name      = UniqName("T_Transform_MatmulMKL_out"),
                                  const common::Target& target = common::DefaultHostTarget());

int GetMulFactor(int shape, const Type& type, const common::Target& target);

/**
 * @brief basic PE that calculates a matrix multiplication
 *
 * @param A The first input tensor, [M, K]
 * @param B The second input tensor, [N, K]
 * @param name The name of the operation
 * @param target if target is x86, we will split the reduce axis
 *
 * @return the output tensors
Notes: this mul only support two-dims-tensor after flattening [M, K] * [N, K], K is the reduce axis
 */
std::vector<ir::Tensor> MulBase(const ir::Tensor& A,
                                const ir::Tensor& B,
                                const std::string& name      = UniqName("T_Transform_MulBase_out"),
                                const common::Target& target = common::DefaultHostTarget());

std::vector<ir::Tensor> Mul(const ir::Tensor& A,
                            const ir::Tensor& B,
                            int x_num_col_dims,
                            const std::vector<ir::Expr>& output_shape,
                            const ir::Var& axis_k,
                            const std::string& name);

std::vector<ir::Tensor> MulMKL(const ir::Tensor& A,
                               const ir::Tensor& B,
                               const std::string& name      = UniqName("T_Transform_MulMKL_out"),
                               const common::Target& target = common::DefaultHostTarget());

std::vector<ir::Tensor> MulBias(const ir::Tensor& A,
                                const ir::Tensor& B,
                                const ir::Tensor& C,
                                int x_num_col_dims,
                                const std::vector<ir::Expr>& output_shape,
                                const ir::Var& axis_k,
                                const std::string& name);

ir::Tensor LayoutTransform(const ir::Tensor& input,
                           const std::string& src_layout,
                           const std::string& dst_layout,
                           const std::string& name = UniqName("T_LayoutTransform_out"));

std::vector<ir::Expr> InferShapeLayoutTransform(const std::vector<Expr>& input_shapes,
                                                const ir::Layout& old_layout,
                                                const ir::Layout& new_layout,
                                                absl::flat_hash_map<int, std::vector<int>>* split_index_map);

/**
 * @brief Perform meta op Reverse
 * @param input The input tensor
 * @param axis reverse axis
 * @param output_name the name of the output tensor
 */
ir::Tensor Reverse(const ir::Tensor& input,
                   const std::vector<int>& axis,
                   const std::string& output_name = UniqName("T_Reverse_out"));

/**
 * @brief Perform meta op Transpose
 * @param input The input tensor
 * @param axis tranpsoe axis
 * @param output_name the name of the output tensor
 */
ir::Tensor Transpose(const ir::Tensor& input,
                     const std::vector<int>& axis,
                     const std::string& output_name = UniqName("T_Transpose_out"));

}  // namespace pe
}  // namespace hlir
}  // namespace cinn
