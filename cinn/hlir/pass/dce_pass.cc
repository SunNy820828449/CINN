// Copyright (c) 2023 CINN Authors. All Rights Reserved.
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

#include <queue>

#include "cinn/common/type.h"
#include "cinn/hlir/pass/op_fusion_pass_util.h"

namespace cinn {
namespace hlir {
namespace pass {

using framework::Graph;
using framework::Node;
using framework::NodeData;
using framework::OpPatternKind;
using framework::shape_t;

using common::GraphEdge;
using common::GraphNode;

using GroupPtr  = std::shared_ptr<Graph::Group>;
using GroupList = std::vector<GroupPtr>;

using ConditionFunction = std::function<bool(const FusionHelperBase*, const Node*, const GroupPtr&)>;

class DceHelper : public FusionHelperBase {
 public:
  DceHelper(Graph* graph) : FusionHelperBase(graph), graph_(graph) {}

  void operator()() {
    for (auto node : output_nodes_set_) {
      WFS(node);
    }

    RemoveDeadNode();
  }

 private:
  void WFS(const Node* node) {
    std::queue<const Node*> candidates;
    candidates.push(node);

    while (!candidates.empty()) {
      auto candidate = candidates.front();
      candidates.pop();
      auto producers = GetProducerNode(candidate);

      for (auto producer : producers) {
        if (nodes_set.count(producer)) {
          continue;
        }
        candidates.push(producer);
        nodes_set.insert(producer);
      }
    }
  }

  void RemoveDeadNode() {
    auto nodes_inorder = std::get<0>(graph_->topological_order());

    for (auto node : nodes_inorder) {
      if (nodes_set.count(node->safe_as<Node>())) {
        continue;
      }

      auto& inlinks  = node->inlinks();
      auto& outlinks = node->outlinks();

      // remove others link to node.
      for (auto link : inlinks) {
        auto src = link->source();
        src->UnLinkAllTo(node);
      }
      // remove node data link to others.
      for (auto link : outlinks) {
        // node data
        auto ndata  = link->sink();
        auto& links = ndata->outlinks();
        for (auto link_ : links) {
          auto dest = link_->sink();
          dest->UnLinkAllTo(ndata);
        }
        graph_->DropNode(ndata);
      }

      graph_->DropNode(node);
    }
  }

  framework::Graph* graph_;
  std::unordered_set<Node*> nodes_set;
};

void DCEPassInternal(Graph* graph) {
  CHECK_GT(graph->outputs.size(), 0);
  DceHelper dce_helper(graph);
  dce_helper();
}

}  // namespace pass
}  // namespace hlir
}  // namespace cinn

CINN_REGISTER_HELPER(DCE) {
  CINN_REGISTER_PASS(DCE)
      .describe("Dce Pass which performs \"Dead code elimination\"")
      .set_change_structure(true)
      .set_body(cinn::hlir::pass::DCEPassInternal);
  return true;
}
