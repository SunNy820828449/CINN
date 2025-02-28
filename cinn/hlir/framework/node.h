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
#include <absl/types/variant.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cinn/common/graph_utils.h"
#include "cinn/common/shared.h"
#include "cinn/hlir/framework/op.h"

namespace cinn {
namespace hlir {
namespace framework {
class Node;
class NodeData;

using NodePtr     = std::shared_ptr<Node>;
using AttrType    = absl::variant<bool,
                               float,
                               int,
                               std::string,
                               std::vector<bool>,
                               std::vector<int>,
                               std::vector<float>,
                               std::vector<std::string>>;
using AttrMapType = absl::flat_hash_map<std::string, AttrType>;

/**
 * \brief Attributes of each node in graph.
 *  The attributes include the node's name, the corresponding operator
 *  and other parameters like axis.
 */
struct NodeAttr {
  using attr_t = AttrType;

  /**
   * \brief The operator this node uses.
   */
  const Operator *op{nullptr};

  /**
   * \brief The name of this node.
   */
  std::string node_name;

  /**
   * \brief The attributes stored as string in dictionary.
   */
  absl::flat_hash_map<std::string, attr_t> attr_store;
};

std::ostream &operator<<(std::ostream &os, const NodeAttr &node_attr);

/**
 * \brief Node represents an operation in a computation graph.
 */
class Node : public common::GraphNode {
 public:
  Node() = default;
  Node(const Operator *op, const std::string &name, std::string id = nullptr) {
    this->attrs.op        = op;
    this->attrs.node_name = name;
    this->id_             = std::move(id);
  }
  const char *type_info() const override { return __type_info__; }
  std::tuple<common::GraphEdge *, common::GraphEdge *> LinkTo(NodeData *other);
  /**
   * \brief Get the unique id of this NodeData.
   */
  std::string id() const override { return id_; }

  /**
   * \brief The attributes in the node.
   */
  NodeAttr attrs;

  //! Get the input tensors in order to match tensors correctly. If do refresh, we will update the links.
  const std::vector<common::Shared<common::GraphEdge>> &inlinks_in_order(bool refresh = false) const;

  //! Get the output tensors in order to match tensors correctly. If do refresh, we will update the links.
  const std::vector<common::Shared<common::GraphEdge>> &outlinks_in_order(bool refresh = false) const;

  inline const Operator *op() const { return this->attrs.op; }

  inline bool is_variable() { return (this->attrs.op == nullptr); }

  inline uint32_t num_outputs() { return is_variable() ? 1 : this->op()->num_outputs; }

  inline uint32_t num_inputs() { return is_variable() ? 1 : this->op()->num_inputs; }

  template <class... Args>
  static NodePtr Create(Args &&... args) {
    return std::make_shared<Node>(std::forward<Args>(args)...);
  }

  static constexpr char *__type_info__ = "hlir_framework_node";

 private:
  /**
   * \brief The unique id of the node.
   */
  std::string id_;
  mutable std::vector<common::Shared<common::GraphEdge>> outlinks_in_order_{};
  mutable std::vector<common::Shared<common::GraphEdge>> inlinks_in_order_{};
};

/**
 * \brief NodeData represents the output data from an operator.
 */
class NodeData : public common::GraphNode {
  using attr_t = AttrType;

 public:
  NodeData(NodePtr node, uint32_t index, uint32_t version, std::string id)
      : source_node(std::move(node)), output_index(index), version(version), id_(std::move(id)) {}

  NodeData() : source_node(), output_index(), version(), id_() {}

  std::tuple<common::GraphEdge *, common::GraphEdge *> LinkTo(Node *other);
  static std::shared_ptr<NodeData> Create(
      const char *op_name,
      std::string node_name,
      std::vector<NodeData> inputs,
      std::string id                                 = nullptr,
      absl::flat_hash_map<std::string, attr_t> attrs = absl::flat_hash_map<std::string, attr_t>()) {
    auto res                           = std::make_shared<NodeData>();
    res->id_                           = std::move(id);
    res->source_node                   = Node::Create();
    res->source_node->attrs.op         = Operator::Get(op_name);
    res->source_node->attrs.node_name  = std::move(node_name);
    res->source_node->attrs.attr_store = attrs;
    return res;
  }

  const char *type_info() const override { return __type_info__; }
  /**
   * \brief Get the unique id of this NodeData.
   */
  std::string id() const override { return id_; }

  /**
   * \brief Source_node represents the operator this NodeData comes from.
   */
  NodePtr source_node;

  /**
   * \brief Output_index represents the index of this output data
   *  among all the outputs of the operator.
   *  For example, if an operator has 2 outputs, the index of
   *  the 2 NodeData should be 0 and 1.
   */
  uint32_t output_index;

  /**
   * \brief The version of input Variable.
   *  This field can only be nonzero when this->node is a Variable node.
   *  version is increased by one each time a Variable get composed to a mutation Op.
   */
  uint32_t version;

  static constexpr char *__type_info__ = "hlir_framework_nodedata";

 private:
  /**
   * \brief The unique id of this NodeData.
   */
  std::string id_;
};

// insert op_node after input_data
NodeData *InsertGraphOpNodeAfter(
    common::Graph *graph, Node *insert_node, NodeData *input_nodedata, Node *dst_node, int pos);
// insert op_node before out_data
NodeData *InsertGraphOpNodeBefore(
    common::Graph *graph, Node *insert_node, Node *input_node, NodeData *dst_data, int pos);

}  // namespace framework
}  // namespace hlir
}  // namespace cinn
