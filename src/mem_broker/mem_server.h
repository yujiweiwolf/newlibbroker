// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "Aeron.h"
#include "FragmentAssembler.h"
#include "mem_struct.h"
#include "flow_control.h"
#include "anti_self_knock_risker.h"
#include "inner_future_master.h"
using namespace aeron;
const int64_t INNER_AERON_STREAM_ID = 2003;

namespace co {

template <typename Broker>
class MemBrokerServer {
 public:
    MemBrokerServer();
    ~MemBrokerServer();

    void Init(MemBrokerOptionsPtr option);
    void Start();
    void Run();

    void SendQueryTradeAsset(MemQueryMessage* msg);
    void OnRspQryAsset(MemTradeAsset* asset);
    void SendQueryTradePosition(MemQueryMessage* msg);
    void OnRspQryPosition(MemTradePosition* pos);
    void SendQueryTradeKnock(MemQueryMessage* msg);
    void OnRspQryKnock(MemTradeKnock* knock);
    void SendTradeOrder(MemTradeOrderMessage* msg);
    void OnRspTradeOrder(MemTradeOrderMessage* msg);
    void SendTradeWithdraw(MemTradeWithdrawMessage* msg);
    void OnRspTradeWithdraw(MemTradeWithdrawMessage* msg);

 private:
    void HandleMessage(const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header& header);

private:
    MemBrokerOptionsPtr opt_;
    Broker broker_;
    std::shared_ptr<Subscription> req_subscription_;
    std::shared_ptr<Publication> rep_publication_;
    std::shared_ptr<Publication> inner_publication_;
    std::shared_ptr<Subscription> inner_subscription_;
    FlowControlQueue flow_control_queue_;
};
}  // namespace co
