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

#include "cinn/frontend/decomposer_registry.h"
#include "cinn/frontend/syntax.h"

namespace cinn {
namespace frontend {
namespace decomposer {

void batch_norm_train(const Instruction& instr, const DecomposerContext& context) {
  CHECK_EQ(instr->inputs.size(), 5UL) << " 5 input tensor for " << instr->op_type;
  CHECK_EQ(instr->outputs.size(), 5UL) << "5 output tensor for " << instr->op_type;

  auto& x            = instr->inputs[0];
  auto& scale        = instr->inputs[1];
  auto& bias         = instr->inputs[2];
  auto& running_mean = instr->inputs[3];
  auto& running_var  = instr->inputs[4];

  float epsilon        = instr.GetAttrs<float>("epsilon");
  std::string layout   = instr.GetAttrs<std::string>("layout");
  float running_factor = instr.GetAttrs<float>("running_factor");

  CinnBuilder* builder   = context.builder();
  std::vector<int> r_dim = {};
  float element_count    = 0;
  int c_dim              = 0;
  if (layout == "NCHW") {
    c_dim         = 1;
    r_dim         = {0, 2, 3};
    element_count = x->shape[0] * x->shape[2] * x->shape[3];
  } else if (layout == "NHWC") {
    c_dim         = 3;
    r_dim         = {0, 1, 2};
    element_count = x->shape[0] * x->shape[1] * x->shape[2];
  } else {
    LOG(FATAL) << layout << " setting is not support!";
  }

  // shape [c]
  auto v_element_count = builder->BroadcastTo(
      builder->ConstScalar<float>(element_count, common::UniqName("element_count")), scale->shape, {0});
  auto v_epsilon =
      builder->BroadcastTo(builder->ConstScalar<float>(epsilon, common::UniqName("epsilon")), scale->shape, {0});

  /*****************batch norm train********************
   * mean = reduce_mean(x)
   * diff = x - mean
   * diff2 = diff * diff
   * var = reduce_mean(diff2)
   * std_var = sqrtf(var)
   * y = diff/std_var * scale + bias
   * running_mean = running_mean * factor + (1.0 - factor) * mean
   * running_var = running_var * factor + (1.0 - factor) * var
   */

  // compute sum, shape = [c]
  auto sum = builder->Reduce(x, ReduceKind::kSum, r_dim);
  // compute mean = [c] -> [n, c, h, w]
  auto save_mean = builder->Div(sum, v_element_count);
  auto mean      = builder->BroadcastTo(save_mean, x->shape, {c_dim});
  // diff
  auto diff  = builder->Sub(x, mean);
  auto _diff = builder->Identity(diff);
  // diff2
  auto diff2 = builder->Mul(diff, _diff);

  // sum variance, shape = [c]
  auto sum_diff2 = builder->Reduce(diff2, ReduceKind::kSum, r_dim);
  // variance, shape[c]
  auto var2 = builder->Div(sum_diff2, v_element_count);
  // standard variance, shape[c] -> [n, c, h, w]
  auto save_var = builder->Add(builder->Sqrt(var2), v_epsilon);
  auto var      = builder->BroadcastTo(save_var, x->shape, {c_dim});

  auto v_scale = builder->BroadcastTo(scale, x->shape, {c_dim});
  auto v_bias  = builder->BroadcastTo(bias, x->shape, {c_dim});
  // (x - mean)/var * scale + bias
  auto y = builder->Add(v_bias, builder->Mul(v_scale, builder->Div(diff, var)));

  // shape = [c]
  auto factor_0 = builder->BroadcastTo(
      builder->ConstScalar<float>(running_factor, common::UniqName("factor_0")), running_mean->shape, {0});
  auto factor_1 = builder->BroadcastTo(
      builder->ConstScalar<float>(1.0f - running_factor, common::UniqName("factor_1")), running_var->shape, {0});
  auto new_mean = builder->Add(builder->Mul(running_mean, factor_0), builder->Mul(save_mean, factor_1));
  auto new_var  = builder->Add(builder->Mul(running_var, factor_0), builder->Mul(save_var, factor_1));

  // map output id
  context.MapOutToOrigin(y, instr->outputs[0]);
  context.MapOutToOrigin(save_mean, instr->outputs[1]);
  context.MapOutToOrigin(save_var, instr->outputs[2]);
  context.MapOutToOrigin(new_mean, instr->outputs[3]);
  context.MapOutToOrigin(new_var, instr->outputs[4]);
}

}  // namespace decomposer
}  // namespace frontend
}  // namespace cinn

CINN_REGISTER_HELPER(batch_norm_train_decomposer) {
  CINN_DECOMPOSER_REGISTER(batch_norm_train, cinn::frontend::decomposer::batch_norm_train);

  return true;
}