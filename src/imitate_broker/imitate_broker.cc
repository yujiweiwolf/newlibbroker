// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "imitate_broker.h"
#include "config.h"

namespace co {
void TestBroker::OnInit() {
    LOG_INFO << "initialize FakeBroker ...";
}

void TestBroker::OnQueryTradeAsset(MemUnionMessage* req) {
     LOG_INFO << "query asset, fund_id: " << req->query.fund_id
              << ", id: " << req->query.id
              << ", timestamp: " << req->query.timestamp;
}

void TestBroker::OnQueryTradePosition(MemUnionMessage* req) {
    LOG_INFO << "query position, fund_id: " << req->query.fund_id
             << ", id: " << req->query.id
             << ", timestamp: " << req->query.timestamp;
}

void TestBroker::OnQueryTradeKnock(MemUnionMessage* req) {
     LOG_INFO << "query knock, fund_id: " << req->query.fund_id
              << ", id: " << req->query.id
              << ", timestamp: " << req->query.timestamp
              << ", cursor: " << req->query.cursor;

}

void TestBroker::OnTradeOrder(MemUnionMessage* req) {

}

void TestBroker::OnTradeWithdraw(MemUnionMessage* msg) {

}
}  // namespace co