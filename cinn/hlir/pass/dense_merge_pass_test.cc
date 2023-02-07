// Copyright (c) 2022 CINN Authors. All Rights Reserved.
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

#include <gtest/gtest.h>

#include "cinn/frontend/decomposer/test_helper.h"

namespace cinn {
namespace frontend {

TEST(DenseMergePass, Test_Matmul_0) {
  NetBuilder net_builder("Test_Matmul_0");
  auto A = net_builder.CreateInput(Float(32), {128, 64}, "A");
  auto B = net_builder.CreateInput(Float(32), {64, 128}, "B");
  auto C = net_builder.CreateInput(Float(32), {64, 128}, "C");
  auto D = net_builder.Matmul(A, B);
  auto E = net_builder.Matmul(A, C);

  auto fetch_ids = {D->id, E->id};
  auto program   = net_builder.Build();
  auto target    = common::DefaultTarget();

  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_ids, target);
  hlir::framework::ApplyPass(graph.get(), "DenseMergePass");

  CHECK_EQ(graph->nodes().size(), 6);
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");
  hlir::framework::ApplyPass(graph.get(), "FusionMergePass");

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto run_program = gc.Build();
}

TEST(DenseMergePass, Test_Matmul_1) {
  NetBuilder net_builder("Test_Matmul_1");
  auto A = net_builder.CreateInput(Float(32), {128, 64}, "A");
  auto B = net_builder.CreateInput(Float(32), {128, 64}, "B");
  auto C = net_builder.CreateInput(Float(32), {64, 128}, "C");
  auto D = net_builder.Matmul(A, C);
  auto E = net_builder.Matmul(B, C);

  auto fetch_ids = {D->id, E->id};
  auto program   = net_builder.Build();
  auto target    = common::DefaultTarget();

  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_ids, target);
  hlir::framework::ApplyPass(graph.get(), "DenseMergePass");
  CHECK_EQ(graph->nodes().size(), 6);
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");
  hlir::framework::ApplyPass(graph.get(), "FusionMergePass");

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto run_program = gc.Build();
}

TEST(DenseMergePass, Test_Matmul_2) {
  NetBuilder net_builder("Test_Matmul_2");
  auto A = net_builder.CreateInput(Float(32), {128, 64}, "A");
  auto B = net_builder.CreateInput(Float(32), {128, 64}, "B");
  auto C = net_builder.CreateInput(Float(32), {128, 64}, "C");
  auto D = net_builder.CreateInput(Float(32), {128, 64}, "D");
  auto E = net_builder.CreateInput(Float(32), {64, 128}, "E");
  auto F = net_builder.Matmul(A, E);
  auto G = net_builder.Matmul(B, E);
  auto H = net_builder.Matmul(C, E);
  auto I = net_builder.Matmul(D, E);

  auto fetch_ids = {F->id, G->id, H->id, I->id};
  auto program   = net_builder.Build();
  auto target    = common::DefaultTarget();

  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_ids, target);
  hlir::framework::ApplyPass(graph.get(), "DenseMergePass");
  CHECK_EQ(graph->nodes().size(), 10);
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");
  hlir::framework::ApplyPass(graph.get(), "FusionMergePass");

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto run_program = gc.Build();
}

TEST(DenseMergePass, Test_Matmul_3) {
  NetBuilder net_builder("Test_Matmul_3");
  auto A = net_builder.CreateInput(Float(32), {128, 64}, "A");
  auto B = net_builder.CreateInput(Float(32), {128, 64}, "B");
  auto C = net_builder.CreateInput(Float(32), {64, 128}, "C");
  auto D = net_builder.CreateInput(Float(32), {128, 64}, "D");
  auto E = net_builder.CreateInput(Float(32), {128, 64}, "E");
  auto F = net_builder.Matmul(A, C);
  auto G = net_builder.Matmul(B, C);
  auto H = net_builder.Matmul(C, D);
  auto I = net_builder.Matmul(C, E);

  auto fetch_ids = {F->id, G->id, H->id, I->id};
  auto program   = net_builder.Build();
  auto target    = common::DefaultTarget();

  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_ids, target);
  hlir::framework::ApplyPass(graph.get(), "DenseMergePass");
  CHECK_EQ(graph->nodes().size(), 11);
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");
  hlir::framework::ApplyPass(graph.get(), "FusionMergePass");

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto run_program = gc.Build();
}

TEST(DenseMergePass, Test_Matmul_4) {
  NetBuilder net_builder("Test_Matmul_4");
  auto A = net_builder.CreateInput(Float(32), {128, 64}, "A");
  auto B = net_builder.CreateInput(Float(32), {128, 64}, "B");
  auto C = net_builder.CreateInput(Float(32), {64, 128}, "C");
  auto D = net_builder.CreateInput(Float(32), {64, 128}, "D");
  auto F = net_builder.Matmul(A, C);
  auto G = net_builder.Matmul(B, C);
  auto H = net_builder.Matmul(A, D);
  auto I = net_builder.Matmul(B, D);

  auto fetch_ids = {F->id, G->id, H->id, I->id};
  auto program   = net_builder.Build();
  auto target    = common::DefaultTarget();

  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_ids, target);
  hlir::framework::ApplyPass(graph.get(), "DenseMergePass");
  CHECK_EQ(graph->nodes().size(), 10);
  hlir::framework::ApplyPass(graph.get(), "OpFusionPass");
  hlir::framework::ApplyPass(graph.get(), "FusionMergePass");

  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto run_program = gc.Build();
}

}  // namespace frontend
}  // namespace cinn
