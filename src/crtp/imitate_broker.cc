// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "imitate_broker.h"

namespace co {

void TestBroker::OnInit() {
    LOG_INFO << __FUNCTION__;
    LOG_INFO << "initialize TestBroker successfully";
}

void TestBroker::OnTradeOrder(MemUnionMessage* msg) {
    LOG_INFO << __FUNCTION__;
    msg->msg_type = kMemTypeTradeOrderRep;
    SendRtnMessage(msg);
    return;
}

}  // namespace co