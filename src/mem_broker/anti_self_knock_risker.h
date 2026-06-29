// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <map>
#include "x/x.h"
#include "coral/coral.h"

namespace co {
const int64_t kOrderTimeoutMS = 3000;

class InnerOrder {
 public:
    bool IsFinished() {
        if (finish_flag) {
            return finish_flag;
        }
        if (withdraw_succeed || match_volume >= volume) {
            finish_flag = true;
            return finish_flag;
        }
        int64_t now = x::UnixMilli();
        // 等待委托响应超时
        if (order_no.empty() && (now - create_time) > kOrderTimeoutMS) {
            LOG_INFO << "委托响应超时, code: " << code
                     << ", message_id: " << message_id
                     << ", bs_flag: " << bs_flag
                     << ", price: " << price
                     << ", volume: " << volume;
            finish_flag = true;
            return finish_flag;
        }
        // 撤单超过设定阈值
        if (order_no.length() > 0 && withdraw_time > 0 && (now - withdraw_time) > kOrderTimeoutMS) {
            LOG_INFO << "撤单失败超过设定阈值, code: " << code
                     << ", message_id: " << message_id
                     << ", order_no: " << order_no
                     << ", bs_flag: " << bs_flag
                     << ", price: " << price
                     << ", volume: " << volume;
            finish_flag = true;
            return finish_flag;
        }
        return false;
    }

 public:
    int64_t create_time = 0;
    std::string message_id;
    int64_t timestamp = 0;
    std::string fund_id;
    std::string code;
    std::string order_no;
    int64_t bs_flag = 0;
    int64_t price = 0;
    int64_t volume = 0;
    int64_t match_volume = 0;
    int64_t withdraw_time = 0;
    bool withdraw_succeed = false;
    bool finish_flag = false;
};
typedef std::shared_ptr<InnerOrder> InnerOrderPtr;

// OrderBook 管理单个证券代码的买卖挂单，用于防对敲检查
class OrderBook {
 public:
    OrderBook() = default;
    ~OrderBook() = default; 

    // 检查是否与现有委托对敲，返回错误信息（空字符串表示通过）
    std::string HandleTradeOrderReq(int64_t bs_flag, int64_t price);

    // 添加委托（买方或卖方）
    void OnTradeOrderReqPass(InnerOrderPtr order);

    InnerOrderPtr HandleTradeOrderRep(const std::string& message_id, int64_t bs_flag, const MemTradeOrder* order);

 private:
    static std::string BuildSelfKnockError(const InnerOrderPtr& other);

 private:
   std::multimap<int64_t, InnerOrderPtr, std::less<int64_t> > asks_;  // price -> orders
   std::multimap<int64_t, InnerOrderPtr, std::greater<int64_t> > bids_;  // price -> orders
};

class AntiSelfKnockRisk {
 public:
    AntiSelfKnockRisk() = default;
    ~AntiSelfKnockRisk() = default;

    // 报单请求检查，返回错误信息（空字符串表示通过）
    std::string HandleTradeOrderReq(MemTradeOrderMessage* req);
    // 报单请求通过，加入订单簿
    void OnTradeOrderReqPass(MemTradeOrderMessage* req);
    // 报单响应处理，更新 order_no
    void HandleTradeOrderRep(MemTradeOrderMessage* rep);

    // 撤单请求检查
    std::string HandleTradeWithdrawReq(MemTradeWithdrawMessage* req);
    // 撤单响应处理
    void HandleTradeWithdrawRep(MemTradeWithdrawMessage* rep);
    // 成交回报处理
    void HandleTradeKnock(MemTradeKnock* knock);

 private:
    std::shared_ptr<OrderBook> MustGetOrderBook(const std::string& code);

 private:
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> order_books_;

    // 先收到成交回报，后收到报单响应
    std::unordered_map<std::string, std::unique_ptr<std::vector<MemTradeKnock>>> knock_first_orders_;

    std::unordered_map<std::string, InnerOrderPtr> single_orders_;
    std::unordered_map<std::string, std::unique_ptr<std::vector<InnerOrderPtr>>> batch_orders_;
};
}  // namespace co
