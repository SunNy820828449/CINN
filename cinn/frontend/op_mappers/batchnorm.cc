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

#include "cinn/frontend/op_mapper_registry.h"
#include "cinn/frontend/op_mappers/common_utils.h"

namespace cinn {
namespace frontend {
namespace op_mappers {

void BatchnormOpMapper(const paddle::cpp::OpDesc& op_desc, const OpMapperContext& ctx) {
  CHECK_EQ(op_desc.Input("X").size(), 1UL);
  auto x_name = op_desc.Input("X").front();
  CHECK_EQ(op_desc.Input("Scale").size(), 1UL);
  auto scale_name = op_desc.Input("Scale").front();
  CHECK_EQ(op_desc.Input("Bias").size(), 1UL);
  auto bias_name = op_desc.Input("Bias").front();
  CHECK_EQ(op_desc.Input("Mean").size(), 1UL);
  auto mean_name = op_desc.Input("Mean").front();
  CHECK_EQ(op_desc.Input("Variance").size(), 1UL);
  auto variance_name = op_desc.Input("Variance").front();
  CHECK(!op_desc.Output("Y").empty());
  auto out_name = op_desc.Output("Y").front();

  auto epsilon     = utils::GetAttrOrDefault<float>(op_desc, "epsilon", 1e-5f);
  auto momentum    = utils::GetAttrOrDefault<float>(op_desc, "momentum", 0.9f);
  auto data_layout = utils::GetAttrOrDefault<std::string>(op_desc, "data_layout", "NCHW");
  auto x           = ctx.GetVar(x_name);
  auto scale       = ctx.GetVar(scale_name);
  auto bias        = ctx.GetVar(bias_name);
  auto mean        = ctx.GetVar(mean_name);
  auto variance    = ctx.GetVar(variance_name);
  auto out         = ctx.Builder()->batchnorm(x, scale, bias, mean, variance, epsilon, momentum, data_layout);

  ctx.AddVar(out_name, out);
  ctx.AddVarModelToProgram(out_name, out->id);
}

}  // namespace op_mappers
}  // namespace frontend
}  // namespace cinn

CINN_REGISTER_HELPER(batchnorm) {
  CINN_REGISTER_OP_MAPPER(batchnorm, cinn::frontend::op_mappers::BatchnormOpMapper)
  return true;
}
