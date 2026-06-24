// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <utility>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include "x/x.h"
#include "coral/coral.h"
#include "../mem_broker/mem_server.h"
#include "../mem_broker/mem_struct.h"

using namespace std;
namespace co {
// 策略类：实现具体的 broker 业务逻辑
class TestBroker {
 public:
    TestBroker() = default;
    ~TestBroker() = default;

    void SetServer(MemBrokerServer<TestBroker>* server) {
        server_ = server;
    }

    void OnInit();
    void SendQueryTradeAsset(MemQueryMessage* req);
    void OnQueryTradePosition(MemQueryMessage* req);
    void OnQueryTradeKnock(MemQueryMessage* req);
    void SendTradeOrder(MemTradeOrderMessage* msg);
    void SendTradeWithdraw(MemTradeWithdrawMessage* msg);

 private:
    void HandReqData();

 private:
    MemBrokerServer<TestBroker>* server_;
};
}  // namespace co
