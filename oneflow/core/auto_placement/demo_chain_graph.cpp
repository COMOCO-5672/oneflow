#include "oneflow/core/auto_placement/demo_chain_graph.h"

namespace oneflow {

namespace df {

DemoChainRegst* DemoChainGraphBuilder::ModelOp(
    const std::string& name, const std::vector<DemoChainRegst*>& inputs) {
  std::vector<DemoChainRegst*> all_inputs(inputs);
  all_inputs.push_back(Model("model_" + name));
  return Op(name, all_inputs);
}

DemoChainRegst* DemoChainGraphBuilder::Op(
    const std::string& name, const std::vector<DemoChainRegst*>& inputs) {
  DemoChainNode* fw_node = NewForwardNode(name);
  fw_node->set_fw_chain_node_id(fw_node->chain_node_id());
  for (auto input : inputs) { Consume(fw_node, input); }
  DemoChainRegst* out = NewRegst(fw_node);
  out->set_diff_handler([=](DemoChainRegst* out_diff) {
    DemoChainNode* bw_node = NewBackwardNode(name);
    bw_node->set_fw_chain_node_id(fw_node->chain_node_id());
    Consume(bw_node, out);
    Consume(bw_node, out_diff);
    for (auto it = inputs.rbegin(); it != inputs.rend(); ++it) {
      Consume(bw_node, *it);
      (*it)->HandleDiff(NewRegst(bw_node));
    }
  });
  return out;
}

DemoChainRegst* DemoChainGraphBuilder::Model(const std::string& name) {
  DemoChainNode* md_node = NewMdUpdtNode(name);
  DemoChainRegst* model = NewRegst(md_node);
  model->set_diff_handler([=](DemoChainRegst* model_diff) {
    DemoChainNode* acc_node = NewDiffAccNode(name);
    Consume(acc_node, model_diff);
    DemoChainRegst* diff_acc_regst = NewRegst(acc_node);
    Consume(md_node, diff_acc_regst);
    int64_t fw_chain_node_id = model_diff->producer()->fw_chain_node_id();
    acc_node->set_fw_chain_node_id(fw_chain_node_id);
    md_node->set_chain_node_id(NewChainNodeId());
    md_node->set_fw_chain_node_id(fw_chain_node_id);
  });
  return model;
}

void DemoChainGraphBuilder::Backward(DemoChainRegst* regst) {
  regst->HandleDiff(NewRegst(regst->mut_producer()));
}

DemoChainRegst* DemoChainGraphBuilder::NewRegst(DemoChainNode* producer) {
  auto regst = of_make_unique<DemoChainRegst>(producer, NewChainRegstId());
  graph_->regsts_.emplace_back(std::move(regst));
  return graph_->regsts_.back().get();
}

DemoChainNode* DemoChainGraphBuilder::NewChainNode(const std::string& name,
                                                   TaskType task_type) {
  auto* node = new DemoChainNode(name, task_type);
  graph_->AddAllocatedNode(node);
  node->set_name(name);
  return node;
}

DemoChainNode* DemoChainGraphBuilder::NewForwardNode(const std::string& name) {
  auto* node = NewChainNode("fw_" + name, TaskType::kNormalForward);
  node->set_chain_node_id(NewChainNodeId());
  return node;
}

DemoChainNode* DemoChainGraphBuilder::NewBackwardNode(const std::string& name) {
  auto* node = NewChainNode("bw_" + name, TaskType::kNormalBackward);
  node->set_chain_node_id(NewChainNodeId());
  return node;
}

DemoChainNode* DemoChainGraphBuilder::NewDiffAccNode(const std::string& name) {
  auto* node = NewChainNode("diff_acc_" + name, TaskType::kMdDiffAcc);
  node->set_chain_node_id(NewChainNodeId());
  return node;
}

DemoChainNode* DemoChainGraphBuilder::NewMdUpdtNode(const std::string& name) {
  return NewChainNode("md_updt_" + name, TaskType::kMdUpdt);
}

void DemoChainGraphBuilder::Consume(DemoChainNode* node,
                                    DemoChainRegst* regst) {
  regst->AddConsumer(node);
  auto edge = new DemoChainEdge(regst);
  graph_->AddAllocatedEdge(edge);
  Connect(regst->mut_producer(), edge, node);
}

size_t DemoChainGraph::FwChainNodeNum() const {
  size_t num = 0;
  ForEachNode([&](const DemoChainNode* node) {
    num += (node->chain_node_id() == node->fw_chain_node_id() ? 1 : 0);
  });
  return num;
}

size_t DemoChainGraph::ChainNodeNum() const {
  size_t num = 0;
  ForEachNode([&](const DemoChainNode* node) { ++num; });
  return num;
}

DemoChainGraph::DemoChainGraph(
    const std::function<void(DemoChainGraphBuilder*)>& Build)
    : chain_node_id_(-1), chain_regst_id_(-1) {
  DemoChainGraphBuilder builder(this);
  Build(&builder);
  InitIsReachable();
  InitRegst2ChainNodeSubGraphs();
}

IsReachablePredicator DemoChainGraph::MakeIsReachablePredicator() const {
  using NodeRelation =
      HashMap<const DemoChainNode*, std::unordered_set<const DemoChainNode*>>;
  std::shared_ptr<NodeRelation> node2ancestors(new NodeRelation());
  TopoForEachChainNode([&](const DemoChainNode* node) {
    ForEachInNode(node, [&](const DemoChainNode* prev) {
      (*node2ancestors)[node].insert((*node2ancestors)[prev].begin(),
                                     (*node2ancestors)[prev].end());
      (*node2ancestors)[node].insert(prev);
    });
  });
  return [=](const DemoChainNode* src, const DemoChainNode* dst) {
    const auto& it = node2ancestors->find(dst);
    if (it == node2ancestors->end()) { return false; }
    return it->second.find(src) != it->second.end();
  };
}

void DemoChainGraph::TopoForEachChainNode(
    const std::function<void(const DemoChainNode*)>& Handler) const {
  std::list<const DemoChainNode*> sources;
  ForEachNode([&](const DemoChainNode* node) {
    int64_t in_cnt = 0;
    ForEachInNode(node, [&](const DemoChainNode*) { ++in_cnt; });
    if (in_cnt == 0) { sources.push_back(node); }
  });

  auto ForEachIn = std::bind(&DemoChainGraph::ForEachInNode, this,
                             std::placeholders::_1, std::placeholders::_2);
  auto ForEachOut = std::bind(&DemoChainGraph::ForEachOutNode, this,
                              std::placeholders::_1, std::placeholders::_2);
  using NodeVisitor = GraphNodeVisitorUtil<const DemoChainNode*>;
  NodeVisitor::TopoForEach(sources, ForEachIn, ForEachOut, Handler);
}

void DemoChainGraph::ForEachInNode(
    const DemoChainNode* node,
    const std::function<void(const DemoChainNode*)>& Handler) const {
  node->ForEachNodeOnInEdge([&](const DemoChainNode* in_node) {
    if (in_node->task_type() == TaskType::kMdDiffAcc) { return; }
    Handler(in_node);
  });
}

void DemoChainGraph::ForEachOutNode(
    const DemoChainNode* node,
    const std::function<void(const DemoChainNode*)>& Handler) const {
  if (node->task_type() == TaskType::kMdDiffAcc) { return; }
  node->ForEachNodeOnOutEdge(Handler);
}

void DemoChainGraph::InitIsReachable() {
  is_reachable_ = MakeIsReachablePredicator();
}

void DemoChainGraph::InitRegst2ChainNodeSubGraphs() {
  for (const auto& regst : regsts_) {
    auto sub_graph = of_make_unique<DemoChainNodeSubGraph>(
        regst->producer(), regst->consumers(), is_reachable_);
    regst2chain_node_sub_graph_.emplace(regst.get(), std::move(sub_graph));
  }
}

std::vector<std::vector<int64_t>>
DemoChainGraph::CalcChainNodeId2FwChainNodeId() const {
  std::vector<std::vector<int64_t>> ret(ChainNodeNum());

  ForEachNode([&](const DemoChainNode* node) {
    CHECK_LT(node->chain_node_id(), ret.size());
    ret.at(node->chain_node_id()).push_back(node->fw_chain_node_id());
  });
  return ret;
}

std::vector<std::vector<int64_t>>
DemoChainGraph::CalcChainRegstId2ProducerChainNodeId() const {
  std::vector<std::vector<int64_t>> ret(regsts_.size());
  for (const auto& regst : regsts_) {
    int64_t chain_node_id = regst->producer()->chain_node_id();
    CHECK_LT(regst->chain_regst_id(), ret.size());
    ret.at(regst->chain_regst_id()).push_back(chain_node_id);
  }
  return ret;
}
std::vector<std::string> DemoChainGraph::CalcChainNodeId2ChainNodeName() const {
  std::vector<std::string> ret(node_num());
  ForEachNode([&](const DemoChainNode* node) {
    ret.at(node->chain_node_id()) = node->name();
  });
  return ret;
}

void DemoChainNodeSubGraph::TopoForEachChainNode(
    const std::function<void(const DemoChainNode*)>& Handler) const {
  auto ForEachIn = std::bind(&DemoChainNodeSubGraph::ForEachInNode, this,
                             std::placeholders::_1, std::placeholders::_2);
  auto ForEachOut = std::bind(&DemoChainNodeSubGraph::ForEachOutNode, this,
                              std::placeholders::_1, std::placeholders::_2);
  using NodeVisitor = GraphNodeVisitorUtil<const DemoChainNode*>;
  NodeVisitor::TopoForEach({start_node_}, ForEachIn, ForEachOut, Handler);
}

void DemoChainNodeSubGraph::ForEachInNode(
    const DemoChainNode* node,
    const std::function<void(const DemoChainNode*)>& Handler) const {
  if (node->task_type() == TaskType::kMdUpdt) {
    node->ForEachNodeOnInEdge(Handler);
  } else {
    node->ForEachNodeOnInEdge([&](const DemoChainNode* in_node) {
      if (IsReachableFromStartNode(in_node)) { Handler(in_node); }
    });
  }
}

void DemoChainNodeSubGraph::ForEachOutNode(
    const DemoChainNode* node,
    const std::function<void(const DemoChainNode*)>& Handler) const {
  if (node->task_type() == TaskType::kMdDiffAcc) {
    node->ForEachNodeOnOutEdge(Handler);
  } else {
    node->ForEachNodeOnOutEdge([&](const DemoChainNode* out_node) {
      if (IsReachableToEndNode(out_node)) { Handler(out_node); }
    });
  }
}

bool DemoChainNodeSubGraph::IsReachableFromStartNode(
    const DemoChainNode* node) const {
  return node == start_node_ || IsReachable(start_node_, node);
}

bool DemoChainNodeSubGraph::IsReachableToEndNode(
    const DemoChainNode* node) const {
  for (const auto* end_node : end_nodes_) {
    if (node == end_node || IsReachable(node, end_node)) { return true; }
  }
  return false;
}

void DemoChainNodeSubGraph::CalcLongestPath(
    std::vector<int64_t>* path,
    const std::function<double(int64_t)>& Time4ChainNodeId) const {
  HashMap<const DemoChainNode*, double> node2longest_path_time;
  HashMap<const DemoChainNode*, std::vector<int64_t>> node2path;
  TopoForEachChainNode([&](const DemoChainNode* node) {
    double time = 0;
    const DemoChainNode* longest_path_tail_node = nullptr;
    ForEachInNode(node, [&](const DemoChainNode* in_node) {
      double t = node2longest_path_time[in_node];
      if (time < t) {
        time = t;
        longest_path_tail_node = in_node;
      }
    });
    node2longest_path_time[node] =
        time + Time4ChainNodeId(node->chain_node_id());
    node2path[node] = node2path[longest_path_tail_node];
    node2path.at(node).push_back(node->chain_node_id());
  });
  double time = 0;
  const DemoChainNode* longest_path_tail_node = nullptr;
  for (const DemoChainNode* node : end_nodes_) {
    double t = node2longest_path_time.at(node);
    if (time < t) {
      time = t;
      longest_path_tail_node = node;
    }
  }
  *path = node2path.at(longest_path_tail_node);
}

std::vector<std::vector<int64_t>>
DemoChainGraph::CalcChainRegstId2PathChainNodeIds(
    const std::function<double(int64_t)>& Time4ChainNodeId) const {
  std::vector<std::vector<int64_t>> ret(regsts_.size());
  for (const auto& pair : regst2chain_node_sub_graph_) {
    int64_t chain_regst_id = pair.first->chain_regst_id();
    pair.second->CalcLongestPath(&ret.at(chain_regst_id), Time4ChainNodeId);
  }
  return ret;
}

std::vector<std::vector<int64_t>> DemoChainGraph::CalcEdgeId2SrcChainNodeId()
    const {
  std::vector<std::vector<int64_t>> ret(edge_num());
  int index = -1;
  ForEachEdge([&](DemoChainEdge* edge) {
    ret.at(++index) = std::vector<int64_t>{edge->src_chain_node_id()};
  });
  return ret;
}

std::vector<std::vector<int64_t>> DemoChainGraph::CalcEdgeId2DstChainNodeId()
    const {
  std::vector<std::vector<int64_t>> ret(edge_num());
  int index = -1;
  ForEachEdge([&](DemoChainEdge* edge) {
    ret.at(++index) = std::vector<int64_t>{edge->dst_chain_node_id()};
  });
  return ret;
}

std::vector<double> DemoChainGraph::RegstId2IsCloned() const {
  std::vector<double> ret(regsts_.size());
  for (const auto& regst : regsts_) {
    ret.at(regst->chain_regst_id()) = (regst->IsRegstCloned() ? 1 : 0);
  }
  return ret;
}

std::vector<double> DemoChainGraph::RegstIIRatio(int piece_num_in_batch) const {
  std::vector<double> ret(regsts_.size());
  for (const auto& regst : regsts_) {
    double ii_ratio = 1;
    if (regst->producer()->task_type() == TaskType::kMdDiffAcc
        || regst->producer()->task_type() == TaskType::kMdUpdt) {
      ii_ratio = piece_num_in_batch;
    }
    ret.at(regst->chain_regst_id()) = ii_ratio;
  }
  return ret;
}

bool DemoChainRegst::IsRegstCloned() const {
  return producer()->task_type() == TaskType::kMdDiffAcc
         || producer()->task_type() == TaskType::kMdUpdt
         || (consumers().size() == 1
             && consumers().front()->task_type() == TaskType::kMdDiffAcc);
}

}  // namespace df
}  // namespace oneflow
