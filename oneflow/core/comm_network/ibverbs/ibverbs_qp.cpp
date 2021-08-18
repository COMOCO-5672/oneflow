/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/comm_network/ibverbs/ibverbs_qp.h"
#include <cstdint>
#include <memory>
#include <vector>
#include "oneflow/core/actor/actor_message.h"
#include "oneflow/core/comm_network/comm_network.h"
#include "oneflow/core/actor/actor_message_bus.h"
#include "oneflow/core/job/resource_desc.h"
#include "oneflow/core/job/global_for.h"
#include "oneflow/core/platform/include/ibv.h"
#include "oneflow/core/comm_network/ibverbs/ibverbs_comm_network.h"

#if defined(WITH_RDMA) && defined(OF_PLATFORM_POSIX)

namespace oneflow {

namespace {

constexpr uint32_t kDefaultQueueDepth = 1024;
constexpr uint64_t kDefaultMemBlockSize = 8388608;  // 8M
//constexpr uint32_t kDefaultMessageSize = 512; // 512 byte

}  // namespace

void MessagePool::RegisterMessagePool(){
      ActorMsg msg;
      size_t ActorMsgSize = sizeof(msg);
    //  std::cout<<"ActorMsgSize:"<<ActorMsgSize << std::endl;
      size_t RegisterMemorySize  = ActorMsgSize  * (num_of_message_);
      char * addr =(char*) malloc(RegisterMemorySize );
      ibv_mr *   mr =ibv::wrapper.ibv_reg_mr_wrap(
          pd_,  addr, RegisterMemorySize,
          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
      CHECK(mr);
      for(size_t i = 0;  i < num_of_message_ ; i++){
          char * split_addr =addr + ActorMsgSize * i ; //这里切割地址没有问题
          ActorMsgMR * msg_mr = new ActorMsgMR(mr,split_addr, ActorMsgSize);
          message_buf_.push_front(msg_mr);
      }
  //     std::cout<<"In RegisterMessagePoo, the size of message_buf_:" << message_buf_.size() << std::endl;
    }

void MessagePool::PutMessage(ActorMsgMR *msg_mr) {
      std::lock_guard<std::mutex>  msg_buf_lck(message_buf_mutex_);
      message_buf_.push_front(msg_mr);
     // std::cout<<"In PutMessage, the size of message_buf_:" << message_buf_.size() << std::endl;
}

ActorMsgMR *  MessagePool::GetMessage(){
  if(isEmpty() == false)  {
    return GetMessageFromBuf();
  } else {
      RegisterMessagePool();
      return GetMessageFromBuf();
  }
}

ActorMsgMR * MessagePool::GetMessageFromBuf() {
  std::lock_guard<std::mutex>  msg_buf_lck(message_buf_mutex_);
  ActorMsgMR * msg_mr = message_buf_.front();
  message_buf_.pop_front();
  //std::cout<<"In GetMessageFromBuf, the size of message_buf_:" << message_buf_.size() << std::endl;
  return msg_mr;
}

IBVerbsQP::IBVerbsQP(ibv_context* ctx, ibv_pd* pd, uint8_t port_num, ibv_cq* send_cq,
                     ibv_cq* recv_cq,
                     std::shared_ptr<MessagePool> recv_msg_buf,
                     std::shared_ptr<MessagePool> send_msg_buf) {
  // ctx_, pd_
  ctx_ = ctx;
 // pd_.reset(pd);
 pd_ = pd ;
  port_num_ = port_num;
  // qp_
  ibv_device_attr device_attr{};
  CHECK_EQ(ibv::wrapper.ibv_query_device(ctx, &device_attr), 0);
  const int64_t user_queue_depth =
      ParseIntegerFromEnv("ONEFLOW_COMM_NET_IB_QUEUE_DEPTH", kDefaultQueueDepth);
  const uint32_t queue_depth = std::min<uint32_t>(device_attr.max_qp_wr, user_queue_depth);
  ibv_qp_init_attr qp_init_attr{};
  qp_init_attr.qp_context = nullptr;
  qp_init_attr.send_cq = send_cq;
  qp_init_attr.recv_cq = recv_cq;
  qp_init_attr.srq = nullptr;
  qp_init_attr.cap.max_send_wr = queue_depth;
  qp_init_attr.cap.max_recv_wr = queue_depth;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  qp_init_attr.cap.max_inline_data = 0;
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.sq_sig_all = 1;
  qp_ = ibv::wrapper.ibv_create_qp(pd, &qp_init_attr);
  CHECK(qp_);
  num_outstanding_send_wr_ = 0;
  max_outstanding_send_wr_ = queue_depth;
  recv_msg_buf_ = recv_msg_buf; //this is no problem 
  send_msg_buf_ = send_msg_buf; //this is no problem 
}

IBVerbsQP::IBVerbsQP(ibv_context* ctx, ibv_pd* pd, uint8_t port_num, ibv_cq* send_cq,
                     ibv_cq* recv_cq) {
  // ctx_, pd_
  ctx_ = ctx;
 // pd_.reset(pd);
  pd_ = pd; 
  port_num_ = port_num;
  // qp_
  ibv_device_attr device_attr{};
  CHECK_EQ(ibv::wrapper.ibv_query_device(ctx, &device_attr), 0);
  const int64_t user_queue_depth =
      ParseIntegerFromEnv("ONEFLOW_COMM_NET_IB_QUEUE_DEPTH", kDefaultQueueDepth);
  const uint32_t queue_depth = std::min<uint32_t>(device_attr.max_qp_wr, user_queue_depth);
  ibv_qp_init_attr qp_init_attr{};
  qp_init_attr.qp_context = nullptr;
  qp_init_attr.send_cq = send_cq;
  qp_init_attr.recv_cq = recv_cq;
  qp_init_attr.srq = nullptr;
  qp_init_attr.cap.max_send_wr = queue_depth;
  qp_init_attr.cap.max_recv_wr = queue_depth;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  qp_init_attr.cap.max_inline_data = 0;
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.sq_sig_all = 1;
  qp_ = ibv::wrapper.ibv_create_qp(pd, &qp_init_attr);
  CHECK(qp_);
  num_outstanding_send_wr_ = 0;
  max_outstanding_send_wr_ = queue_depth;
}

IBVerbsQP::~IBVerbsQP() {
  CHECK_EQ(ibv::wrapper.ibv_destroy_qp(qp_), 0);
  // while (send_msg_buf_.empty() == false) {
  //   delete send_msg_buf_.front();
  //   send_msg_buf_.pop();
  // }
  // while(recv_msg_buf_.em)
  //todo lambda:这里要加上recv_msg_buf_和sendMsgbuf_的析构
 // for (ActorMsgMR* msg_mr : recv_msg_buf_) { delete msg_mr; }
}

void IBVerbsQP::Connect(const IBVerbsConnectionInfo& peer_info) {
  ibv_qp_attr qp_attr{};

  // IBV_QPS_INIT
  memset(&qp_attr, 0, sizeof(ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_INIT;
  // TODO(liujuncheng): Make pkey_index configurable
  qp_attr.pkey_index = 0;
  qp_attr.port_num = port_num_;
  qp_attr.qp_access_flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
  CHECK_EQ(ibv::wrapper.ibv_modify_qp(
               qp_, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS),
           0);

  // IBV_QPS_RTR
  memset(&qp_attr, 0, sizeof(ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_RTR;
  // TODO(liujuncheng): Make sl configurable;
  qp_attr.ah_attr.sl = 0;
  qp_attr.ah_attr.src_path_bits = 0;
  if (peer_info.lid() == 0) {
    qp_attr.ah_attr.is_global = 1;
    qp_attr.ah_attr.grh.dgid.global.subnet_prefix = peer_info.subnet_prefix();
    qp_attr.ah_attr.grh.dgid.global.interface_id = peer_info.interface_id();
    qp_attr.ah_attr.grh.flow_label = 0;
    const int64_t gid_index = ParseIntegerFromEnv("ONEFLOW_COMM_NET_IB_GID_INDEX", 0);
    qp_attr.ah_attr.grh.sgid_index = gid_index;
    qp_attr.ah_attr.grh.hop_limit = 255;
    // TODO(liujuncheng): Make traffic_class configurable;
    qp_attr.ah_attr.grh.traffic_class = 0;
  } else {
    qp_attr.ah_attr.is_global = 0;
    qp_attr.ah_attr.dlid = peer_info.lid();
  }
  qp_attr.ah_attr.port_num = peer_info.port_num();
  qp_attr.path_mtu = static_cast<ibv_mtu>(peer_info.mtu());
  qp_attr.dest_qp_num = peer_info.qp_num();
  qp_attr.rq_psn = 0;
  qp_attr.max_dest_rd_atomic = 1;
  qp_attr.min_rnr_timer = 12;
  CHECK_EQ(ibv::wrapper.ibv_modify_qp(qp_, &qp_attr,
                                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
                                          | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC
                                          | IBV_QP_MIN_RNR_TIMER),
           0);

  // IBV_QPS_RTS
  memset(&qp_attr, 0, sizeof(ibv_qp_attr));
  qp_attr.qp_state = IBV_QPS_RTS;
  qp_attr.sq_psn = 0;
  qp_attr.max_rd_atomic = 1;
  qp_attr.retry_cnt = 7;
  qp_attr.rnr_retry = 7;
  qp_attr.timeout = 14;
  CHECK_EQ(ibv::wrapper.ibv_modify_qp(qp_, &qp_attr,
                                      IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC
                                          | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_TIMEOUT),

           0);
}

void IBVerbsQP::PostAllRecvRequest() {
    if(recv_msg_buf_->isEmpty()) {
      recv_msg_buf_->RegisterMessagePool();
      size_t size = recv_msg_buf_->size();
      size_t i = 0;
      while(recv_msg_buf_->isEmpty() == false && i < size) {
        ActorMsgMR * msg_mr = recv_msg_buf_->GetMessage();
        PostRecvRequest(msg_mr);
        i++;
      }
    } else {
        size_t size = recv_msg_buf_->size();
        size_t i = 0;
        while(recv_msg_buf_->isEmpty() == false && i < size ) {
        ActorMsgMR * msg_mr = recv_msg_buf_->GetMessage();
        PostRecvRequest(msg_mr);
        i++;
      }
    }
}

void IBVerbsQP::PostReadRequest(const IBVerbsCommNetRMADesc& remote_mem,
                                const IBVerbsMemDesc& local_mem, void* read_id) {
  CHECK_EQ(remote_mem.mem_size, local_mem.mem_size());
  WorkRequestId* wr_id = NewWorkRequestId();
  const size_t block_size =
      ParseIntegerFromEnv("ONEFLOW_COMM_NET_IB_MEM_BLOCK_SIZE", kDefaultMemBlockSize);
  const size_t block_num = RoundUp(remote_mem.mem_size, block_size) / block_size;
  wr_id->outstanding_sge_cnt = static_cast<int32_t>(block_num);
  wr_id->read_id = read_id;
  FOR_RANGE(size_t, i, 0, block_num) {
    ibv_send_wr wr{};
    ibv_sge sge{};
    sge.addr = reinterpret_cast<uint64_t>(local_mem.mem_ptr()) + i * block_size;
    sge.length = std::min(block_size, local_mem.mem_size() - i * block_size);
    sge.lkey = local_mem.mr()->lkey;
    wr.wr_id = reinterpret_cast<uint64_t>(wr_id);
    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = 0;
    wr.imm_data = 0;
    wr.wr.rdma.remote_addr = remote_mem.mem_ptr + i * block_size;
    wr.wr.rdma.rkey = remote_mem.mr_rkey;
    EnqueuePostSendReadWR(wr, sge);
  }
}

void IBVerbsQP::PostSendRequest(const ActorMsg& msg) {
 // std::cout<<"In PostSendRequest,the msg.comm_net_sequence_token:" << msg.comm_net_sequence_number() << std::endl;
  ActorMsgMR * msg_mr = send_msg_buf_->GetMessage();
  msg_mr->set_msg(msg);
  WorkRequestId* wr_id = NewWorkRequestId();
  wr_id->msg_mr = msg_mr;
  ibv_send_wr wr{};
  ibv_sge sge{};
  sge.addr = reinterpret_cast<uint64_t>(reinterpret_cast<void*>(msg_mr->addr()));
  sge.length = msg_mr->size();
  sge.lkey = msg_mr->lkey();
  wr.wr_id = reinterpret_cast<uint64_t>(wr_id);
  wr.next = nullptr;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND;
  wr.send_flags = 0;
  wr.imm_data = 0;
  memset(&(wr.wr), 0, sizeof(wr.wr));
  EnqueuePostSendReadWR(wr, sge);
}

void IBVerbsQP::EnqueuePostSendReadWR(ibv_send_wr wr, ibv_sge sge) {
  std::unique_lock<std::mutex> pending_send_wr_lock_(pending_send_wr_mutex_);
  if (num_outstanding_send_wr_ < max_outstanding_send_wr_) {
    num_outstanding_send_wr_++;
    ibv_send_wr* bad_wr = nullptr;
    CHECK_EQ(ibv_post_send(qp_, &wr, &bad_wr), 0);
  } else {
    std::pair<ibv_send_wr, ibv_sge> ibv_send_wr_sge = std::make_pair(wr, sge);
    pending_send_wr_queue_.push(ibv_send_wr_sge);
  }
}

void IBVerbsQP::ReadDone(WorkRequestId* wr_id) {
  CHECK_GE(wr_id->outstanding_sge_cnt, 1);
  wr_id->outstanding_sge_cnt -= 1;
  if (wr_id->outstanding_sge_cnt == 0) {
    Global<CommNet>::Get()->ReadDone(wr_id->read_id);
    DeleteWorkRequestId(wr_id);
  }
  PostPendingSendWR();
}

void IBVerbsQP::SendDone(WorkRequestId* wr_id) {
  {
    send_msg_buf_->PutMessage(wr_id->msg_mr);
  }
  DeleteWorkRequestId(wr_id);
  PostPendingSendWR();
}

void IBVerbsQP::RecvDone(WorkRequestId* wr_id) {
  auto* ibv_comm_net = dynamic_cast<IBVerbsCommNet*>(Global<CommNet>::Get());
  CHECK(ibv_comm_net != nullptr);
  ibv_comm_net->RecvActorMsg(wr_id->msg_mr->msg());
  PostRecvRequest(wr_id->msg_mr);
  recv_msg_buf_->PutMessage(wr_id->msg_mr);
  DeleteWorkRequestId(wr_id);
}

void IBVerbsQP::PostPendingSendWR() {
  std::unique_lock<std::mutex> pending_send_wr_lock_(pending_send_wr_mutex_);
  if (pending_send_wr_queue_.empty() == false) {
    std::pair<ibv_send_wr, ibv_sge> ibv_send_wr_sge = std::move(pending_send_wr_queue_.front());
    ibv_send_wr wr = ibv_send_wr_sge.first;
    wr.sg_list = &ibv_send_wr_sge.second;
    pending_send_wr_queue_.pop();
    ibv_send_wr* bad_wr = nullptr;
    CHECK_EQ(ibv_post_send(qp_, &wr, &bad_wr), 0);
  } else {
    if (num_outstanding_send_wr_ > 0) { num_outstanding_send_wr_--; }
  }
}

void IBVerbsQP::PostRecvRequest(ActorMsgMR* msg_mr) {
  WorkRequestId* wr_id = NewWorkRequestId();
  wr_id->msg_mr = msg_mr;
  ibv_recv_wr wr{};
  ibv_sge sge{};
  sge.addr = reinterpret_cast<uint64_t>(reinterpret_cast<void*>(msg_mr->addr()));
  sge.length = msg_mr->size();
  sge.lkey = msg_mr->lkey();
  wr.wr_id = reinterpret_cast<uint64_t>(wr_id);
  wr.next = nullptr;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  ibv_recv_wr* bad_wr = nullptr;
  CHECK_EQ(ibv_post_recv(qp_, &wr, &bad_wr), 0);
}

WorkRequestId* IBVerbsQP::NewWorkRequestId() {
  WorkRequestId* wr_id = new WorkRequestId;
  wr_id->qp = this;
  wr_id->outstanding_sge_cnt = 0;
  wr_id->read_id = nullptr;
  wr_id->msg_mr = nullptr;
  return wr_id;
}

void IBVerbsQP::DeleteWorkRequestId(WorkRequestId* wr_id) {
  CHECK_EQ(wr_id->qp, this);
  delete wr_id;
}

}  // namespace oneflow

#endif  // WITH_RDMA && OF_PLATFORM_POSIX
