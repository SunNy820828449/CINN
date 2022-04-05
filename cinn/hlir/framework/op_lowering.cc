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

#include "cinn/hlir/framework/op_lowering.h"

namespace cinn {
namespace hlir {
namespace framework {

using framework::Graph;
using framework::Node;
using framework::NodeData;
using framework::OpPatternKind;
using framework::shape_t;
using framework::StrategyFunction;

using common::GraphEdge;
using common::GraphNode;
using common::Type;
using namespace lang;

NodeData* GetNodeData(Node* node) {
  auto node_data = (*node->outlinks().begin())->sink()->safe_as<NodeData>();
  CHECK(node_data);
  return node_data;
}

std::vector<Node*> GetConsumer(Node* node) {
  std::vector<Node*> consumers;
  auto node_data = GetNodeData(node);
  for (auto& link : node_data->outlinks()) {
    auto consumer_node = link->sink()->safe_as<Node>();
    CHECK(consumer_node);
    consumers.push_back(consumer_node);
  }
  return consumers;
}

OpLoweringHelper::OpLoweringHelper(const absl::flat_hash_map<std::string, Type>& type_dict,
                                   const absl::flat_hash_map<std::string, shape_t>& shape_dict,
                                   const Target& target)
    : type_dict_(type_dict), shape_dict_(shape_dict), target_(target) {}

std::vector<ir::LoweredFunc> OpLoweringHelper::Lowering(const Group& group) {
  switch (group->op_pattern_kind) {
    case framework::kElemWise:
    case framework::kBroadcast:
    case framework::kInjective:
      return ElementwiseOpLowering(group);
    case framework::kCommReduce:
      return ReduceOpLowering(group);
    case framework::kOutEWiseFusable:
      return FusableOpLowering(group);
    case framework::kOpaque:
      return OpaqueOpLowering(group);
    default:
      return OpaqueOpLowering(group);
  }
}

// elementwise fusion op lowering
std::vector<ir::LoweredFunc> OpLoweringHelper::ElementwiseOpLowering(const Group& group) {
  // get input tensor and output tensor
  poly::StageMap stages;
  std::string func_name = "fn_fuse";
  std::vector<ir::Tensor> func_args;
  std::unordered_map<std::string, ir::Tensor> tensor_map;

  auto compute = [this, &group, &stages, &tensor_map, &func_name, &func_args](const Group& sub_group) {
    auto& strategy        = Operator::GetAttrs<StrategyFunction>("CINNStrategy");
    auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");

    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      func_name += "_" + node->id();

      std::vector<ir::Tensor> tensor_inputs;
      std::vector<common::CINNValue> cinn_inputs;
      // get all input nodes
      for (auto& link : node->inlinks_in_order(true)) {
        auto source = link->source();
        CHECK(source);
        auto source_data = source->safe_as<NodeData>();
        CHECK(source_data);

        if (tensor_map.count(source_data->id())) {
          tensor_inputs.push_back(tensor_map[source_data->id()]);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor_map[source_data->id()])));
        } else {
          auto tensor = lang::Placeholder<float>(source_data->id(), this->shape_dict_.at(source_data->id()));
          tensor_map[source_data->id()] = tensor;
          stages->InsertLazily(tensor);

          tensor_inputs.push_back(tensor);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor)));
          // recored func input args
          func_args.push_back(tensor);
        }
      }

      std::vector<Type> out_types;
      std::vector<std::vector<int>> out_shapes;

      out_types.push_back(this->type_dict_.at(node_data->id()));
      out_shapes.push_back(this->shape_dict_.at(node_data->id()));

      auto impl = OpStrategy::SelectImpl(
          strategy[node->op()](node->attrs, tensor_inputs, out_types, out_shapes, this->target_));
      common::CINNValuePack C = impl->fcompute(common::CINNValuePack{cinn_inputs});

      if (group->master_nodes.count(node)) {
        C = impl->fschedule(C);
      }

      CHECK(C.size() == 2);
      Expr out                  = C[0];
      poly::StageMap tmp_stages = C.back();

      tensor_map[node_data->id()] = out.as_tensor_ref();
      stages->InsertLazily(out.as_tensor_ref(), tmp_stages[out.as_tensor_ref()]);
    }
  };

  if (group->fused_sub_groups.size() == 0) {
    compute(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      compute(sub_group);
    }
  }

  auto schedule = [this, &group, &stages, &tensor_map](const Group& sub_group) {
    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      // if group master node
      if (group->master_nodes.count(node)) {
        continue;
      }

      LOG(INFO) << node->id();
      // if node is fringe node or internal node, fringe node is output node of sub-graph
      if (group->output_nodes.count(node) || group->internal_nodes.count(node) ||
          sub_group->internal_nodes.count(node)) {
        auto master_node_data = GetNodeData(*group->master_nodes.begin());
        // stage
        auto master_node_stage = stages[tensor_map[master_node_data->id()]];
        auto node_stage        = stages[tensor_map[node_data->id()]];
        // copy schedule from master node
        node_stage->CopyTransform(master_node_stage);
        node_stage->CopyLoopInfo(master_node_stage);
        // internal node use buffer
        if (group->internal_nodes.count(node) || sub_group->internal_nodes.count(node)) {
          node_stage->SetBuffer("local");
        }
        // compute at master node
        node_stage->SimpleComputeAt(master_node_stage, master_node_stage->n_out_dims() - 1);
        continue;
      }
      LOG(INFO) << node->id();
      // others elemenwise internal node use compute-inline
      stages[tensor_map[node_data->id()]]->ComputeInline();
    }
  };

  if (group->fused_sub_groups.size() == 0) {
    schedule(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      schedule(sub_group);
    }
  }

  for (auto& node : group->output_nodes) {
    auto tensor = tensor_map[GetNodeData(node)->id()];
    func_args.push_back(tensor);
  }

  return lang::LowerVec(func_name, stages, func_args, {}, {}, nullptr, this->target_);
}

std::vector<ir::LoweredFunc> OpLoweringHelper::ReduceOpLowering(const Group& group) {
  // get input tensor and output tensor
  poly::StageMap stages;
  std::string func_name = "fn_fuse";
  std::vector<ir::Tensor> func_args;
  std::unordered_map<std::string, ir::Tensor> tensor_map;

  auto compute = [this, &group, &stages, &tensor_map, &func_name, &func_args](const Group& sub_group) {
    auto& strategy        = Operator::GetAttrs<StrategyFunction>("CINNStrategy");
    auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");

    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      func_name += "_" + node->id();

      std::vector<ir::Tensor> tensor_inputs;
      std::vector<common::CINNValue> cinn_inputs;
      // get all input nodes
      for (auto& link : node->inlinks_in_order(true)) {
        auto source = link->source();
        CHECK(source);
        auto source_data = source->safe_as<NodeData>();
        CHECK(source_data);

        if (tensor_map.count(source_data->id())) {
          tensor_inputs.push_back(tensor_map[source_data->id()]);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor_map[source_data->id()])));
        } else {
          auto tensor = lang::Placeholder<float>(source_data->id(), this->shape_dict_.at(source_data->id()));
          tensor_map[source_data->id()] = tensor;
          stages->InsertLazily(tensor);

          tensor_inputs.push_back(tensor);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor)));
          // recored func input args
          func_args.push_back(tensor);
        }
      }

      std::vector<Type> out_types;
      std::vector<std::vector<int>> out_shapes;

      out_types.push_back(this->type_dict_.at(node_data->id()));
      out_shapes.push_back(this->shape_dict_.at(node_data->id()));

      auto impl =
          OpStrategy::SelectImpl(strategy[node->op()](node->attrs, tensor_inputs, out_types, out_shapes, target_));
      common::CINNValuePack C = impl->fcompute(common::CINNValuePack{cinn_inputs});

      CHECK(C.size() == 2 || C.size() == 3 || C.size() == 4);
      Expr out                  = C[0];
      poly::StageMap tmp_stages = C.back();

      // node is kCommReduce
      if (op_pattern_dict[node->op()] == framework::kCommReduce) {
        C = impl->fschedule(C);
      } else if (group->master_nodes.count(node)) {
        // node is master node, copy schedule from reduce node
        for (auto rnode : group->master_nodes) {
          if (op_pattern_dict[rnode->op()] == framework::kCommReduce) {
            auto rnode_data = GetNodeData(rnode);
            tmp_stages[out.as_tensor_ref()]->CopyTransform(stages[tensor_map[rnode_data->id()]]);
            tmp_stages[out.as_tensor_ref()]->CopyLoopInfo(stages[tensor_map[rnode_data->id()]]);
            break;
          }
        }
      }

      if (C.size() >= 2) {
        tensor_map[node_data->id()] = out.as_tensor_ref();
        stages->InsertLazily(out.as_tensor_ref(), tmp_stages[out.as_tensor_ref()]);
      }

      if (C.size() >= 3) {
        Expr out_0                         = C[1];
        tensor_map[node_data->id() + "_0"] = out_0.as_tensor_ref();
        stages->InsertLazily(out_0.as_tensor_ref(), tmp_stages[out_0.as_tensor_ref()]);
      }

      if (C.size() == 4) {
        Expr out_1                         = C[2];
        tensor_map[node_data->id() + "_1"] = out_1.as_tensor_ref();
        stages->InsertLazily(out_1.as_tensor_ref(), tmp_stages[out_1.as_tensor_ref()]);
      }
    }
  };

  if (group->fused_sub_groups.size() == 0) {
    compute(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      compute(sub_group);
    }
  }

  // for reduce fusion op, there are 4 situations
  // element-wise + reduce(vertical)
  // reduce + element-wise(vertical)
  // reduce + reduce(horizontal)
  // (reduce + reduce(horizontal)) + elementwise(vertical)
  auto schedule = [this, &group, &stages, &tensor_map](const Group& sub_group) {
    // assign reduce input tensor schedule, do loop transform.
    auto assign_reduce = [this, &stages](ir::Tensor input, const std::vector<int>& axes) {
      // reorder none-last reduce axis to last.
      // like: shape = [16,16,16,16,16],axes = [1,3] -> new order = [0, 2, 4, 1, 3].
      std::vector<int> order;
      auto shape = input->shape;
      for (int idx = 0; idx < shape.size(); ++idx) {
        if (std::find(axes.begin(), axes.end(), idx) == axes.end()) {
          order.push_back(idx);
        }
      }
      for (auto axis : axes) {
        order.push_back(axis);
      }
      stages[input]->Reorder(order);

      // fuse others none-reduce axis.
      int last_dimension_num = shape.size() - axes.back() - 1;
      int index              = shape.size() - last_dimension_num - axes.size();
      // fuse last_dimension_num - 1 times
      for (auto idx = index; idx < index + last_dimension_num - 1; ++idx) {
        stages[input]->Fuse(index, index + 1);
      }

      if (stages[input]->GetDimRange(index) > 1024) {
        stages[input]->Split(index, 1024);
      }

      // fuse index - 1 times
      for (int idx = 0; idx < index - 1; ++idx) {
        stages[input]->Fuse(0, 1);
      }
    };
    auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");
    Node* master_node     = nullptr;
    for (auto node : group->master_nodes) {
      if (op_pattern_dict[node->op()] != framework::kCommReduce) {
        master_node = node;
        break;
      }
    }

    // if not find master node, using last kCommReduce as master node.
    if (!master_node) {
      if (group->fused_sub_groups.empty()) {
        master_node = group->nodes.front();
      } else {
        master_node = group->fused_sub_groups.back()->nodes.front();
      }
      CHECK_EQ(op_pattern_dict[master_node->op()], framework::kCommReduce) << "Master Node Type Must Be Reduce!";
    }
    auto master_node_data = GetNodeData(master_node);
    auto master_stage     = stages[tensor_map[master_node_data->id()]];
    // get master reducer
    auto GetReducerNode = [&op_pattern_dict](const Group& g) -> Node* {
      for (auto& node : g->master_nodes) {
        if (op_pattern_dict[node->op()] == framework::kCommReduce) {
          return node;
        }
      }
      return nullptr;
    };
    Node* master_reducer = nullptr;
    for (int idx = group->fused_sub_groups.size() - 1; idx >= 0; --idx) {
      master_reducer = GetReducerNode(group->fused_sub_groups[idx]);
    }
    if (!master_reducer) {
      master_reducer = GetReducerNode(group);
    }
    auto master_reducer_data  = GetNodeData(master_reducer);
    auto master_reducer_stage = stages[tensor_map[master_reducer_data->id()]];
    auto master_reducer_axis  = absl::get<std::vector<int>>(master_reducer->attrs.attr_store.at("dim"));
    auto master_reducer_shape = this->shape_dict_.at(master_reducer->inlinks_in_order()[0]->source()->id());

    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      auto stage     = stages[tensor_map[node_data->id()]];
      // if node is kCommReduce
      if (node == master_node) {
        continue;
      }

      // if node is kCommReduce
      if (op_pattern_dict[node->op()] == framework::kCommReduce) {
        // if node is not output node, set buffer.
        if (!group->output_nodes.count(node)) {
          stage->SetBuffer("local");
        }

        // last dimension is in reduce, use BlockReduceInternal to reduce
        if (std::find(master_reducer_axis.begin(), master_reducer_axis.end(), master_reducer_shape.size() - 1) ==
            master_reducer_axis.end()) {
          // compute at last dimension
          if (node == master_reducer) {
            stage->SimpleComputeAt(master_stage, master_stage->n_out_dims() - 1);
          } else {
            stage->SimpleComputeAt(master_reducer_stage, master_reducer_stage->n_out_dims() - 1);
          }
        } else {
          //
          if (node == master_reducer) {
            // schedule loop > 1, compute at 0
            if (master_stage->n_out_dims() > 1) {
              stage->SimpleComputeAt(master_stage, 0);
            }
          } else if (tensor_map.count(node_data->id() + "_1")) {
            auto stage_1 = stages[tensor_map[node_data->id() + "_1"]];
            auto stage_2 = stages[tensor_map[master_reducer_data->id() + "_1"]];
            // compute at master reducer
            stage_1->SimpleComputeAt(stage_2, stage_2->n_out_dims() - 1);
            // delete stage_1 compute at stage_0
            auto stage_0 = stages[tensor_map[node_data->id() + "_0"]];
            stage_1->GetComputeAts().erase(stage_0->id());
          }

          if (master_reducer_stage->n_out_dims() > 1) {
            stage->SimpleComputeAt(master_reducer_stage, 0);
          }
        }
      }
      continue;

      // if node is internal node or output, try to copy schedule from fellow node
      if (group->output_nodes.count(node) || group->internal_nodes.count(node) ||
          sub_group->internal_nodes.count(node)) {
        // checkout node data size is before reduce or after reduce
        if (this->shape_dict_.at(node_data->id()) == this->shape_dict_.at(master_node_data->id())) {
          stage->CopyTransform(master_stage);
          stage->CopyLoopInfo(master_stage);
          if (group->internal_nodes.count(node) || sub_group->internal_nodes.count(node)) {
            stage->SetBuffer("local");
          }
          // fringe node with no consumer
          stage->SimpleComputeAt(master_stage, master_stage->n_out_dims() - 1);
          continue;
        }
        // node is before reduce
        if (std::find(master_reducer_axis.begin(), master_reducer_axis.end(), master_reducer_shape.size() - 1) ==
            master_reducer_axis.end()) {
          // node can't be output node.
          // CHECK(!group->output_nodes.count(node));
          if (!group->output_nodes.count(node) && !group->internal_nodes.count(node) &&
              !sub_group->internal_nodes.count(node)) {
            stage->ComputeInline();
          } else {
            assign_reduce(tensor_map[node_data->id()], master_reducer_axis);
            stage->SimpleComputeAt(master_reducer_stage, master_reducer_stage->n_out_dims() - 1);
          }
        } else {
          std::vector<int> reducer_axes(master_reducer_axis.begin(), master_reducer_axis.end() - 1);
          // compute succesive reduce dimension size
          int last_reduce_size = master_reducer_shape.back();
          for (int idx = master_reducer_axis.size() - 2; idx >= 0; --idx) {
            if (master_reducer_axis[idx] != master_reducer_axis[idx + 1] - 1) {
              break;
            }
            last_reduce_size *= master_reducer_shape[master_reducer_axis[idx]];
            if (last_reduce_size <= this->target_.max_num_threads()) {
              reducer_axes.resize(idx);
            }
          }

          // if last_reduce_size is over 1024, ComputeInline
          if (last_reduce_size > this->target_.max_num_threads() && reducer_axes.size() > 0) {
            CHECK(tensor_map.count(master_reducer_data->id() + "_1"));
            auto reducer_stage = stages[tensor_map[master_reducer_data->id() + "_1"]];
            assign_reduce(tensor_map[node_data->id()], reducer_axes);
            stage->SimpleComputeAt(reducer_stage, reducer_stage->n_out_dims() - 1);
          } else {
            // compute at reduce node
            auto reducer_stage = stages[tensor_map[master_reducer_data->id() + "_0"]];
            stage->CopyTransform(reducer_stage);
            stage->CopyLoopInfo(reducer_stage);
            if (group->internal_nodes.count(node) || sub_group->internal_nodes.count(node)) {
              stage->SetBuffer("local");
            }
            stage->SimpleComputeAt(reducer_stage, reducer_stage->n_out_dims() - 1);
          }
        }
        continue;
      }
      // others elemenwise internal node use compute-inline
      stage->ComputeInline();
    }
  };

  if (group->fused_sub_groups.empty()) {
    schedule(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      schedule(sub_group);
    }
  }

  for (auto& node : group->output_nodes) {
    auto tensor = tensor_map[GetNodeData(node)->id()];
    func_args.push_back(tensor);
  }

  return lang::LowerVec(func_name, stages, func_args, {}, {}, nullptr, this->target_);
}  // namespace framework

std::vector<ir::LoweredFunc> OpLoweringHelper::FusableOpLowering(const Group& group) {
  // get input tensor and output tensor
  poly::StageMap stages;
  std::string func_name = "fn_fuse";
  std::vector<ir::Tensor> func_args;
  std::unordered_map<std::string, ir::Tensor> tensor_map;

  auto compute = [this, &group, &stages, &tensor_map, &func_name, &func_args](const Group& sub_group) {
    auto& strategy        = Operator::GetAttrs<StrategyFunction>("CINNStrategy");
    auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");

    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      func_name += "_" + node->id();

      std::vector<ir::Tensor> tensor_inputs;
      std::vector<common::CINNValue> cinn_inputs;
      // get all input nodes
      for (auto& link : node->inlinks_in_order(true)) {
        auto source = link->source();
        CHECK(source);
        auto source_data = source->safe_as<NodeData>();
        CHECK(source_data);

        if (tensor_map.count(source_data->id())) {
          tensor_inputs.push_back(tensor_map[source_data->id()]);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor_map[source_data->id()])));
        } else {
          auto tensor = lang::Placeholder<float>(source_data->id(), this->shape_dict_.at(source_data->id()));
          tensor_map[source_data->id()] = tensor;
          stages->InsertLazily(tensor);

          tensor_inputs.push_back(tensor);
          cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor)));
          // recored func input args
          func_args.push_back(tensor);
        }
      }

      std::vector<Type> out_types;
      std::vector<std::vector<int>> out_shapes;

      out_types.push_back(this->type_dict_.at(node_data->id()));
      out_shapes.push_back(this->shape_dict_.at(node_data->id()));

      auto impl =
          OpStrategy::SelectImpl(strategy[node->op()](node->attrs, tensor_inputs, out_types, out_shapes, target_));
      common::CINNValuePack C = impl->fcompute(common::CINNValuePack{cinn_inputs});

      CHECK_GE(C.size(), 2);
      ir::Expr out              = C[0];
      poly::StageMap tmp_stages = C.back();
      // node is kCommReduce
      if (op_pattern_dict[node->op()] == framework::kOutEWiseFusable) {
        C = impl->fschedule(C);
      } else if (group->master_nodes.count(node)) {
        // node is master node, copy schedule from OutEWiseFusable node
        for (auto fnode : group->master_nodes) {
          if (op_pattern_dict[fnode->op()] == framework::kOutEWiseFusable) {
            auto fnode_data = GetNodeData(fnode);
            tmp_stages[out.as_tensor_ref()]->CopyTransform(stages[tensor_map[fnode_data->id()]]);
            tmp_stages[out.as_tensor_ref()]->CopyLoopInfo(stages[tensor_map[fnode_data->id()]]);
            break;
          }
        }
      }

      std::string postfix = "";
      for (auto idx = 0; idx < C.size() - 1; ++idx) {
        ir::Expr out                          = C[idx];
        tensor_map[node_data->id() + postfix] = out.as_tensor_ref();
        stages->InsertLazily(out.as_tensor_ref(), tmp_stages[out.as_tensor_ref()]);
        // update postfix
        postfix = "_" + std::to_string(idx);
      }
    }
  };

  if (group->fused_sub_groups.size() == 0) {
    compute(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      compute(sub_group);
    }
  }

  // there are 2 situations for kOutEWiseFusable fuse kElementwise/kBroadcast.
  // kOutEWiseFusable + Elementwise(vertical)
  // kOutEWiseFusable + kElementwise/kBroadcast(horizontal)
  auto schedule = [this, &group, &stages, &tensor_map](const Group& sub_group) {
    auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");
    Node* master_node     = nullptr;
    for (auto node : group->master_nodes) {
      if (op_pattern_dict[node->op()] != framework::kOutEWiseFusable) {
        master_node = node;
        break;
      }
    }

    // if not find master node, using last kOutEWiseFusable as master node.
    if (!master_node) {
      if (group->fused_sub_groups.empty()) {
        master_node = group->nodes.front();
      } else {
        master_node = group->fused_sub_groups.back()->nodes.front();
      }
      CHECK_EQ(op_pattern_dict[master_node->op()], framework::kOutEWiseFusable)
          << "Master Node Type Must Be OutEWiseFusable!";
    }

    auto master_node_data = GetNodeData(master_node);
    auto master_stage     = stages[tensor_map[master_node_data->id()]];

    for (int idx = sub_group->nodes.size() - 1; idx >= 0; --idx) {
      auto node      = sub_group->nodes[idx];
      auto node_data = GetNodeData(node);
      auto stage     = stages[tensor_map[node_data->id()]];
      // if node is master node.
      if (node == master_node) {
        continue;
      }

      // if node is kOutEWiseFusable
      if (op_pattern_dict[node->op()] == framework::kOutEWiseFusable) {
        // if node is not output nodes
        if (!group->output_nodes.count(node)) {
          tensor_map[node_data->id()]->WithBuffer("local");
        }
        // use compute at master node
        stage->SimpleComputeAt(master_stage, master_stage->n_out_dims() - 1);
        continue;
      }

      // if node is internal node or output, try to copy schedule from fellow node
      if (group->output_nodes.count(node) || group->internal_nodes.count(node) ||
          sub_group->internal_nodes.count(node)) {
        // copy transform from master node
        stage->CopyTransform(master_stage);
        stage->CopyLoopInfo(master_stage);

        if (group->internal_nodes.count(node) || sub_group->internal_nodes.count(node)) {
          stage->SetBuffer("local");
        }
        // fringe node with no consumer
        stage->SimpleComputeAt(master_stage, master_stage->n_out_dims() - 1);
        continue;
      }
      // others elemenwise internal node use compute-inline
      stage->ComputeInline();
    }
  };

  if (group->fused_sub_groups.size() == 0) {
    schedule(group);
  } else {
    for (auto& sub_group : group->fused_sub_groups) {
      schedule(sub_group);
    }
  }

  for (auto& node : group->output_nodes) {
    auto tensor = tensor_map[GetNodeData(node)->id()];
    func_args.push_back(tensor);
  }

  return lang::LowerVec(func_name, stages, func_args, {}, {}, nullptr, this->target_);
}

std::vector<ir::LoweredFunc> OpLoweringHelper::OpaqueOpLowering(const Group& group) {
  // get input tensor and output tensor
  std::vector<ir::Tensor> func_args;
  CHECK_EQ(group->nodes.size(), 1) << "fusion op exist more than 1 op.";
  auto& strategy        = Operator::GetAttrs<StrategyFunction>("CINNStrategy");
  auto& op_pattern_dict = Operator::GetAttrs<OpPatternKind>("OpPattern");

  auto node = *group->nodes.begin();
  std::vector<ir::Tensor> tensor_inputs;
  std::vector<common::CINNValue> cinn_inputs;
  for (auto& link : node->inlinks_in_order(true)) {
    auto source = link->source();
    CHECK(source);
    auto source_data = source->safe_as<NodeData>();
    CHECK(source_data);

    auto tensor = lang::Placeholder<float>(source_data->id(), this->shape_dict_.at(source_data->id()));
    tensor_inputs.push_back(tensor);

    cinn_inputs.push_back(common::CINNValue(ir::Expr(tensor)));
    // recored func input args
    func_args.push_back(tensor);
  }

  std::vector<Type> out_types;
  std::vector<std::vector<int>> out_shapes;

  auto node_data = GetNodeData(node);
  out_types.push_back(this->type_dict_.at(node_data->id()));
  out_shapes.push_back(this->shape_dict_.at(node_data->id()));

  auto impl = OpStrategy::SelectImpl(strategy[node->op()](node->attrs, tensor_inputs, out_types, out_shapes, target_));
  common::CINNValuePack C = impl->fcompute(common::CINNValuePack{cinn_inputs});
  C                       = impl->fschedule(C);

  CHECK(C.size() >= 2);
  poly::StageMap stages = C.back();
  // lazily insert input tensor.
  for (auto tensor_input : tensor_inputs) {
    stages->InsertLazily(tensor_input);
  }

  for (int idx = 0; idx < C.size() - 1; ++idx) {
    Expr out = C[0];
    // collect output tensor
    func_args.push_back(out.as_tensor_ref());
  }

  return lang::LowerVec("fn_" + node->id(), stages, func_args, {}, {}, nullptr, this->target_);
}

}  // namespace framework
}  // namespace hlir
}  // namespace cinn
