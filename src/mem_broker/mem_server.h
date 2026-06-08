// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "mem_base_broker.h"
#include "iox2/iceoryx2.hpp"
using namespace iox2;

namespace co {
class MemBrokerServer {
 public:
    MemBrokerServer();

    ~MemBrokerServer();

    void Init(MemBrokerOptionsPtr option, MemBaseBrokerPtr broker);

    void SendQueryTradeAsset(MemUnionMessage* msg);
    void SendQueryTradePosition(MemUnionMessage* msg);
    void SendQueryTradeKnock(MemUnionMessage* msg);
    void SendTradeOrder(MemUnionMessage* msg);
    void SendTradeWithdraw(MemUnionMessage* msg);

private:
    MemBaseBrokerPtr broker_;

};
}  // namespace co

