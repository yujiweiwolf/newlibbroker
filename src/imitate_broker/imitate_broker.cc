// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "imitate_broker.h"
#include "config.h"

namespace co {
void TestBroker::OnInit() {
    LOG_INFO << "initialize FakeBroker ...";
}

void TestBroker::SendQueryTradeAsset(MemQueryMessage* req) {
     LOG_INFO << "query asset, fund_id: " << req->fund_id
              << ", id: " << req->id
              << ", timestamp: " << req->timestamp;
    MemTradeAsset asset{};
    asset.usable = 1000;
    asset.timestamp = req->timestamp;
    strcpy(asset.fund_id, req->fund_id);
    server_->OnRspQryAsset(&asset);
}

void TestBroker::OnQueryTradePosition(MemQueryMessage* req) {
    LOG_INFO << "query position, fund_id: " << req->fund_id
             << ", id: " << req->id
             << ", timestamp: " << req->timestamp;
}

void TestBroker::OnQueryTradeKnock(MemQueryMessage* req) {
    LOG_INFO << "query knock, fund_id: " << req->fund_id
             << ", id: " << req->id
             << ", timestamp: " << req->timestamp;

}

void TestBroker::SendTradeOrder(MemTradeOrderMessage* req) {
    LOG_INFO << ToString(req);
    for (int i = 0; i < req->items_size; ++i) {
        auto& order = req->items[i];
        sprintf(order.order_no, "1_MDBC%d", i);
        LOG_INFO << ToString(&order);
    }
    server_->OnRspTradeOrder(req);
}

void TestBroker::OnTradeWithdraw(MemTradeWithdrawMessage* msg) {

}
}  // namespace co