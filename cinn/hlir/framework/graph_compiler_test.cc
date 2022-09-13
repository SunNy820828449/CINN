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

#include "cinn/hlir/framework/graph_compiler.h"

#include <gtest/gtest.h>

#include "cinn/frontend/net_builder.h"
#include "cinn/frontend/optimize.h"
#include "cinn/frontend/program_pass.h"
#include "cinn/hlir/framework/pass.h"
#include "cinn/hlir/framework/scope.h"
#include "cinn/hlir/op/use_ops.h"
#include "cinn/hlir/pass/use_pass.h"
#include "cinn/utils/data_util.h"

namespace cinn {
namespace hlir {
namespace framework {

using common::Float;
using frontend::Placeholder;

TEST(GraphCompilerTest, TestRemoveInvaildVariables) {
  frontend::NetBuilder builder("test");
  auto a = builder.CreateInput(Float(32), {1, 64, 112, 112}, "A");
  auto b = builder.CreateInput(Float(32), {64}, "B");

  auto c = builder.Add(a, b, 1);
  auto d = builder.Relu(c);

  auto target = common::DefaultHostTarget();
  auto graph  = std::make_shared<Graph>(builder.Build(), target);

  // OpFusion will fuse add+relu, and the intermediate variable 'c' is eliminated
  ApplyPasses(graph.get(), frontend::DefaultOpFusionPasses());
  LOG(INFO) << graph->Visualize();
  auto scope = BuildScope(target, graph);
  ASSERT_EQ(scope->var_names().size(), 4);
  EXPECT_NE(scope->FindVar(c->id), nullptr);

  GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  ASSERT_EQ(scope->var_names().size(), 3);
  EXPECT_EQ(scope->FindVar(c->id), nullptr);

  ASSERT_NO_THROW(runtime_program->Execute());
}

TEST(GraphCompilerTest, TestInsertBufferHandlers) {
  frontend::NetBuilder builder("test");
  auto a = builder.CreateInput(Float(32), {1, 64, 112, 112}, "A");
  auto b = builder.CreateInput(Float(32), {64}, "B");

  auto c      = builder.Add(a, b, 1);
  auto d      = builder.Relu(c);
  auto target = common::DefaultHostTarget();
  auto graph  = std::make_shared<Graph>(builder.Build(), target);
  ApplyPasses(graph.get(), frontend::DefaultOpFusionPasses());
  auto scope = BuildScope(target, graph);

  GraphCompiler gc_disable(target, scope, graph);
  GraphCompiler::CompileOptions options;
  // disable with_buffer_handle_instruction_inserted: only 1 instruction
  auto runtime_program_disable = gc_disable.Build(options).runtime_program;
  ASSERT_EQ(runtime_program_disable->size(), 1);
  const auto& computation_instr_disable = runtime_program_disable->GetRunInstructions().front();

  // enable with_buffer_handle_instruction_inserted: 3 instructions, 1st ->
  // malloc instruction(a, b, d), 2nd -> the real computation
  // instruction(add + relu)  and 3rd -> free instruction
  GraphCompiler gc_enable(target, scope, graph);
  options.with_buffer_handle_instruction_inserted = true;
  auto runtime_program_enable                     = gc_enable.Build(options).runtime_program;
  const auto& instructions                        = runtime_program_enable->GetRunInstructions();
  ASSERT_EQ(instructions.size(), 3);

  const auto& malloc_instr = instructions.front();
  ASSERT_EQ(malloc_instr->size(), 1);
  auto malloc_variable_names = malloc_instr->GetInArgs().front();
  auto used_variable_names   = std::unordered_set<std::string>(
      {static_cast<frontend::Variable>(a)->id, static_cast<frontend::Variable>(b)->id, d->id});
  EXPECT_EQ(malloc_instr->GetFnNames().size(), 1);
  EXPECT_EQ(malloc_instr->GetFnNames().front(), "malloc_buffer_instruction_0");
  EXPECT_EQ(malloc_instr->GetOutArgs().size(), 1);
  EXPECT_TRUE(malloc_instr->GetOutArgs().front().empty());
  EXPECT_EQ(malloc_variable_names.size(), 3);
  EXPECT_EQ(std::unordered_set<std::string>(malloc_variable_names.begin(), malloc_variable_names.end()),
            used_variable_names);

  const auto& computation_instr_enable = instructions.at(1);
  ASSERT_EQ(computation_instr_disable->size(), computation_instr_enable->size());
  auto computation_instr_function_names = computation_instr_enable->GetFnNames();
  ASSERT_EQ(computation_instr_disable->GetFnNames().size(), computation_instr_enable->GetFnNames().size());
  ASSERT_NE(computation_instr_function_names.front().find("fn_elementwise_add_0_relu_1_fused"), std::string::npos);

  EXPECT_EQ(computation_instr_disable->GetInArgs(), computation_instr_enable->GetInArgs());
  EXPECT_EQ(computation_instr_disable->GetOutArgs(), computation_instr_enable->GetOutArgs());

  const auto& free_instr = instructions.back();
  ASSERT_EQ(free_instr->size(), 1);
  EXPECT_EQ(free_instr->GetFnNames().size(), 1);
  EXPECT_EQ(free_instr->GetFnNames().front(), "free_buffer_instruction_0");
  EXPECT_EQ(free_instr->GetInArgs().size(), 1);
  EXPECT_TRUE(free_instr->GetInArgs().front().empty());
  auto free_variable_names = free_instr->GetOutArgs().front();
  EXPECT_EQ(std::unordered_set<std::string>(free_variable_names.begin(), free_variable_names.end()),
            used_variable_names);
}

#ifdef CINN_WITH_CUDA
std::vector<float> test_mul(const std::vector<float>& A, const std::vector<float>& B, int M, int K, int N) {
  std::vector<float> C_target(M * N);
  for (int i = 0; i < M; i++) {
    for (int j = 0; j < N; j++) {
      for (int k = 0; k < K; k++) {
        C_target[i * N + j] += A[i * K + k] * B[k * N + j];
      }
    }
  }
  return C_target;
}

void RunCublas(int M, int N, int K) {
  frontend::NetBuilder net_builder("builder");
  auto A = net_builder.CreateInput(Float(32), {M, K}, "A");
  auto B = net_builder.CreateInput(Float(32), {K, N}, "B");
  auto C = net_builder.Matmul(A, B);

  auto program = net_builder.Build();
  auto target  = common::DefaultTarget();
  auto graph   = std::make_shared<hlir::framework::Graph>(program, target);

  hlir::framework::ApplyPass(graph.get(), "MatmulToCublasCustomCallPass");
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");

  auto scope = BuildScope(target, graph);
  GraphCompiler gc(target, scope, graph);
  auto exe_program = gc.Build();

  auto data_a = scope->GetTensor("A");
  auto data_b = scope->GetTensor("B");
  SetRandData<float>(data_a, target);
  SetRandData<float>(data_b, target);
  exe_program->Execute();
  auto data_c = scope->GetTensor(C->id);

  auto host_a = GetTensorData<float>(data_a, target);
  auto host_b = GetTensorData<float>(data_b, target);
  auto host_c = GetTensorData<float>(data_c, target);

  auto target_mul = test_mul(host_a, host_b, M, K, N);
  for (int i = 0; i < data_c->shape().numel(); i++) {
    // LOG_FIRST_N(INFO, 10) << "cinn_data[" << i << "]: " <<  target_mul[i]
    //                       << " v.s. target_data[" << i << "]: " << host_c[i];
    EXPECT_NEAR(host_c[i], target_mul[i], 1e-4);
  }
}

TEST(GraphCompilerTest, TestCublas) {
  RunCublas(64, 64, 128);
  RunCublas(64, 32, 128);
  RunCublas(64, 128, 128);
}

#endif

}  // namespace framework
}  // namespace hlir
}  // namespace cinn
