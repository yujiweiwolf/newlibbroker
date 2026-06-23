// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "iox2/iceoryx2.hpp"
#include "mem_struct.h"
using namespace iox2;

namespace co {

template <typename Broker>
class MemBrokerServer {
 public:
    // using BrokerPtr = std::shared_ptr<Broker>;

    MemBrokerServer() {}

    ~MemBrokerServer() {}

    void Init(MemBrokerOptionsPtr option) {
        broker_.SetServer(this);
        broker_.OnInit();
    }

    void SendQueryTradeAsset(MemQueryMessage* msg) {
        broker_.SendQueryTradeAsset(msg);
    }

    void OnRspQryAsset(MemTradeAsset* asset) {
        LOG_INFO << "OnRspQryAsset " << ToString(asset);
    }

    void SendTradeOrder(MemTradeOrderMessage* msg) {
        broker_.SendTradeOrder(msg);
    }

    void OnRspTradeOrder(MemTradeOrderMessage* msg) {
        LOG_INFO << "OnRspTradeOrder " << ToString(msg);
        for (int i = 0; i < msg->items_size; ++i) {
            auto order = msg->items[i];
            LOG_INFO << ToString(&order);
        }
    }

 private:
    Broker broker_;
};

}  // namespace co
