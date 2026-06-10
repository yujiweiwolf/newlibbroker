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

    void OnInit();
    void OnQueryTradeAsset(MemUnionMessage* req);
    void OnQueryTradePosition(MemUnionMessage* req);
    void OnQueryTradeKnock(MemUnionMessage* req);
    void OnTradeOrder(MemUnionMessage* msg);
    void OnTradeWithdraw(MemUnionMessage* msg);

 private:
    void HandReqData();

 private:
    string spot_fund_id_;
    string future_fund_id_;
    string option_fund_id_;
    int64_t order_no_index_ = 0;
    int64_t match_no_index_ = 0;

    std::shared_ptr<std::thread> rep_thread_ = nullptr;
    std::mutex mutex_;
    std::unordered_map<std::string, std::pair<int64_t, void*>> all_req_;
    std::unordered_map<std::string, std::pair<int64_t, MemTradeOrder>> all_order_;
    std::unordered_map<std::string, std::vector<std::string>> all_batch_;
};
}  // namespace co
