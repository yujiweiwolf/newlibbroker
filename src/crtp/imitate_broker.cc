// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "imitate_broker.h"

namespace co {

void TestBroker::OnInit() {
    LOG_INFO << __FUNCTION__;
    LOG_INFO << "initialize TestBroker successfully";
}

void TestBroker::OnTradeOrder(MemTradeOrderMessage* msg) {
    LOG_INFO << __FUNCTION__;
    SendRtnMessage(msg);
    return;
}

}  // namespace co