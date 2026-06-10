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
        broker_.OnInit();
    };

    void SendQueryTradeAsset(MemUnionMessage* msg) {
        broker_.OnQueryTradeAsset(msg);
    }

 private:
    Broker broker_;
};

}  // namespace co
