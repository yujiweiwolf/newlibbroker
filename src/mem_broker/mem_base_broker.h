// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "options.h"
#include "mem_struct.h"

namespace co {

class MemBrokerServer;

class MemBaseBroker {
 public:
    MemBaseBroker();
    virtual ~MemBaseBroker();

 protected:
    virtual void OnInit();

    virtual void OnQueryTradeAsset(MemUnionMessage* req);

    virtual void OnQueryTradePosition(MemUnionMessage* req);

    virtual void OnQueryTradeKnock(MemUnionMessage* req);

    virtual void OnTradeOrder(MemUnionMessage* req);

    virtual void OnTradeWithdraw(MemUnionMessage* req);
};

typedef std::shared_ptr<MemBaseBroker> MemBaseBrokerPtr;
}  // namespace co

