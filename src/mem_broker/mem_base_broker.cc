// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "mem_base_broker.h"
#include "mem_server.h"

namespace co {
MemBaseBroker::MemBaseBroker() {
}

MemBaseBroker::~MemBaseBroker() {
}

void MemBaseBroker::OnInit() {

}

void MemBaseBroker::OnQueryTradeAsset(MemUnionMessage* req) {
    throw std::runtime_error("BrokerUnimplementedError");
}

void MemBaseBroker::OnQueryTradePosition(MemUnionMessage* req) {
    throw std::runtime_error("BrokerUnimplementedError");
}

void MemBaseBroker::OnQueryTradeKnock(MemUnionMessage* req) {
    throw std::runtime_error("BrokerUnimplementedError");
}

void MemBaseBroker::OnTradeOrder(MemUnionMessage* req) {
    throw std::runtime_error("BrokerUnimplementedError");
}

void MemBaseBroker::OnTradeWithdraw(MemUnionMessage* req) {
    throw std::runtime_error("BrokerUnimplementedError");
}

}  // namespace co

