// Copyright 2026 Fancapital Inc.  All rights reserved.
#include <gtest/gtest.h>
#include <cstring>

#define private public
#include "../mem_broker/anti_self_knock_risker.h"
#undef private

namespace co {

// ============================================================
// OrderBook 测试
// ============================================================

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        buy_order_ = std::make_shared<InnerOrder>();
        buy_order_->message_id = x::UUID();
        buy_order_->fund_id = "fund_1";
        buy_order_->code = "600000.SH";
        buy_order_->bs_flag = kBsFlagBuy;
        buy_order_->price = 99800;      // f2i(9.98)
        buy_order_->volume = 1000;
        buy_order_->timestamp = x::UnixMilli();
        buy_order_->create_time = x::UnixMilli();

        sell_order_ = std::make_shared<InnerOrder>();
        sell_order_->message_id = x::UUID();
        sell_order_->fund_id = "fund_1";
        sell_order_->code = "600000.SH";
        sell_order_->bs_flag = kBsFlagSell;
        sell_order_->price = 99800;     // f2i(9.98)
        sell_order_->volume = 1000;
        sell_order_->timestamp = x::UnixMilli();
        sell_order_->create_time = x::UnixMilli();
    }

    InnerOrderPtr buy_order_;
    InnerOrderPtr sell_order_;
};

// 测试场景：同 fund_id 买单价格 >= 最低卖价，应检测到对敲
TEST_F(OrderBookTest, BuyCrossWithSameFundSell) {
    OrderBook book;
    book.OnTradeOrderReqPass(sell_order_);

    // 买入价 99900 >= 卖出价 99800，应交叉
    std::string err = book.HandleTradeOrderReq(kBsFlagBuy, 99900);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);
}

// 测试场景：买单价格低于最低卖价，应通过
TEST_F(OrderBookTest, BuyPriceBelowAskPass) {
    OrderBook book;
    book.OnTradeOrderReqPass(sell_order_);

    // 买入价 90000 < 卖出价 99800，不应交叉
    std::string err = book.HandleTradeOrderReq(kBsFlagBuy, 90000);
    EXPECT_TRUE(err.empty());
}

// 测试场景：同 fund_id 卖单价格 <= 最高买价，应检测到对敲
TEST_F(OrderBookTest, SellCrossWithSameFundBuy) {
    OrderBook book;
    book.OnTradeOrderReqPass(buy_order_);

    // 卖出价 99700 <= 买入价 99800，应交叉
    std::string err = book.HandleTradeOrderReq(kBsFlagSell, 99700);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);
}

// 测试场景：卖单价格高于最高买价，应通过
TEST_F(OrderBookTest, SellPriceAboveBidPass) {
    OrderBook book;
    book.OnTradeOrderReqPass(buy_order_);

    // 卖出价 99900 > 买入价 99800，不应交叉
    std::string err = book.HandleTradeOrderReq(kBsFlagSell, 99900);
    EXPECT_TRUE(err.empty());
}

// 测试场景：已完成委托应被清理
TEST_F(OrderBookTest, FinishedOrderCleaned) {
    OrderBook book;
    book.OnTradeOrderReqPass(sell_order_);

    // 标记为已完成
    sell_order_->finish_flag = true;

    // 买入价 99800 >= 卖出价 99800，应交叉检查但对手已完成，应通过
    std::string err = book.HandleTradeOrderReq(kBsFlagBuy, 99800);
    EXPECT_TRUE(err.empty());

    // 验证已完成委托已被移除
    EXPECT_EQ(book.asks_.size(), 0);
}

// 测试场景：HandleTradeOrderRep 按 message_id 查找
TEST_F(OrderBookTest, HandleTradeOrderRepFound) {
    OrderBook book;
    book.OnTradeOrderReqPass(sell_order_);

    // 使用正确的 order_no 构造 MemTradeOrder
    MemTradeOrder item;
    memset(&item, 0, sizeof(item));
    item.bs_flag = kBsFlagSell;
    item.price = i2f(sell_order_->price);
    strncpy(item.order_no, "order_001", kMemOrderNoSize);

    auto result = book.HandleTradeOrderRep(sell_order_->message_id, sell_order_->bs_flag, &item);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->fund_id, "fund_1");
}

// 测试场景：HandleTradeOrderRep 无 order_no 时删除
TEST_F(OrderBookTest, HandleTradeOrderRepNoOrderNoErase) {
    OrderBook book;
    book.OnTradeOrderReqPass(sell_order_);

    EXPECT_EQ(book.asks_.size(), 1);

    MemTradeOrder item;
    memset(&item, 0, sizeof(item));
    item.bs_flag = kBsFlagSell;
    item.price = 9.98;
    // order_no 为空

    auto result = book.HandleTradeOrderRep(sell_order_->message_id, kBsFlagSell, &item);
}

// ============================================================
// AntiSelfKnockRisk 测试
// ============================================================

static void FillOrderItem(MemTradeOrder* item, const char* code, int64_t bs_flag,
                          double price, int64_t volume, const char* order_no = "") {
    memset(item, 0, sizeof(MemTradeOrder));
    strncpy(item->code, code, kMemCodeSize - 1);
    item->bs_flag = bs_flag;
    item->price = price;
    item->volume = volume;
    item->timestamp = x::UnixMilli();
    if (strlen(order_no) > 0) {
        strncpy(item->order_no, order_no, kMemOrderNoSize - 1);
    }
}

class AntiSelfKnockRiskTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 构造报单消息：1个item
        msg_ = (MemTradeOrderMessage*)malloc(sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
        memset(msg_, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
        strncpy(msg_->id, x::UUID().c_str(), kMemIdSize - 1);
        strncpy(msg_->fund_id, "fund_a", kMemFundIdSize);
        msg_->items_size = 1;
        msg_->timestamp = x::UnixMilli();
        FillOrderItem(&msg_->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);

        // 构造报单响应消息
        rep_ = (MemTradeOrderMessage*)malloc(sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
        memset(rep_, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
        strncpy(rep_->id, msg_->id, kMemIdSize);
        strncpy(rep_->fund_id, "fund_a", kMemFundIdSize);
        rep_->items_size = 1;
        rep_->timestamp = x::UnixMilli();
        FillOrderItem(&rep_->items[0], "600000.SH", kBsFlagSell, 9.98, 1000, "order_001");

        // 构造撤单消息
        memset(&withdraw_msg_, 0, sizeof(withdraw_msg_));
        strncpy(withdraw_msg_.id, x::UUID().c_str(), kMemIdSize - 1);
        strncpy(withdraw_msg_.fund_id, "fund_a", kMemFundIdSize);
        strncpy(withdraw_msg_.order_no, "order_001", kMemOrderNoSize);
        withdraw_msg_.timestamp = x::UnixMilli();

        // 构造成交回报
        memset(&knock_, 0, sizeof(knock_));
        strncpy(knock_.fund_id, "fund_a", kMemFundIdSize);
        strncpy(knock_.match_no, "match_001", kMemMatchNoSize);
        strncpy(knock_.order_no, "order_001", kMemOrderNoSize);
        strncpy(knock_.code, "600000.SH", kMemCodeSize);
        knock_.match_type = kMatchTypeOK;
        knock_.match_volume = 500;
        knock_.timestamp = x::UnixMilli();
    }

    void TearDown() override {
        free(msg_);
        free(rep_);
    }

    MemTradeOrderMessage* msg_;
    MemTradeOrderMessage* rep_;
    MemTradeWithdrawMessage withdraw_msg_;
    MemTradeKnock knock_;
    AntiSelfKnockRisk risk_;
};

// 测试场景：空订单应返回错误
TEST_F(AntiSelfKnockRiskTest, HandleTradeOrderReq_NullRequest) {
    std::string err = risk_.HandleTradeOrderReq(nullptr);
    EXPECT_FALSE(err.empty());
}

// 测试场景：首次报单（无对手单），应通过
TEST_F(AntiSelfKnockRiskTest, FirstOrderPass) {
    std::string err = risk_.HandleTradeOrderReq(msg_);
    EXPECT_TRUE(err.empty());
}

// 测试场景：同 fund_id 同价格买卖单对敲检测
TEST_F(AntiSelfKnockRiskTest, SelfKnockDetected) {
    // 先发一个卖单
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);

    // 构造同 fund_id 的买单
    auto buy_msg_id = x::UUID();
    MemTradeOrderMessage* buy_msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(buy_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(buy_msg->id, buy_msg_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg->fund_id, "fund_a", kMemFundIdSize);
    buy_msg->items_size = 1;
    buy_msg->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(buy_msg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);

    free(buy_msg);
}

// 测试场景：报单响应后，成交回报能找到订单
TEST_F(AntiSelfKnockRiskTest, TradeKnockAfterRep) {
    // 添加卖单并响应
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);
    risk_.HandleTradeOrderRep(rep_);

    // 成交回报
    risk_.OnTradeKnock(&knock_);

    // 内部检查 single_orders_ 应仍存在（部分成交）
    auto it = risk_.single_orders_.find("order_001");
    ASSERT_NE(it, risk_.single_orders_.end());
    EXPECT_EQ(it->second->match_volume, 500);
}

// 测试场景：全部成交后订单被删除
TEST_F(AntiSelfKnockRiskTest, FullyKnockedOrderRemoved) {
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);
    risk_.HandleTradeOrderRep(rep_);

    // 两次成交各500，总计1000，应全部成交
    knock_.match_volume = 500;
    risk_.OnTradeKnock(&knock_);
    risk_.OnTradeKnock(&knock_);

    auto it = risk_.single_orders_.find("order_001");
    EXPECT_EQ(it, risk_.single_orders_.end());
}

// 测试场景：先成交后回报，缓存成交
TEST_F(AntiSelfKnockRiskTest, KnockBeforeRep) {
    // 先成交，此时还没有订单
    risk_.OnTradeKnock(&knock_);

    // 验证已缓存
    auto kit = risk_.knock_first_orders_.find("order_001");
    ASSERT_NE(kit, risk_.knock_first_orders_.end());
    EXPECT_EQ(kit->second->size(), 1);

    // 报单通过并响应
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);
    risk_.HandleTradeOrderRep(rep_);

    // 成交应已被处理
    auto it = risk_.single_orders_.find("order_001");
    ASSERT_NE(it, risk_.single_orders_.end());
    EXPECT_EQ(it->second->match_volume, 500);
}

// 测试场景：撤单请求和响应
TEST_F(AntiSelfKnockRiskTest, WithdrawOrder) {
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);
    risk_.HandleTradeOrderRep(rep_);

    // 撤单请求
    std::string err = risk_.HandleTradeWithdrawReq(&withdraw_msg_);
    EXPECT_TRUE(err.empty());

    // 撤单响应
    risk_.HandleTradeWithdrawRep(&withdraw_msg_);

    auto it = risk_.single_orders_.find("order_001");
    ASSERT_NE(it, risk_.single_orders_.end());
    EXPECT_TRUE(it->second->withdraw_succeed);
}


// 测试场景：不同股票代码不互相影响
TEST_F(AntiSelfKnockRiskTest, DiffCodeNoSelfKnock) {
    // 卖600000
    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg_).empty());
    risk_.OnTradeOrderReqPass(msg_);

    // 买600001（不同代码）
    auto buy_msg_id = x::UUID();
    MemTradeOrderMessage* buy_msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(buy_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(buy_msg->id, buy_msg_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg->fund_id, "fund_a", kMemFundIdSize);
    buy_msg->items_size = 1;
    buy_msg->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg->items[0], "600001.SH", kBsFlagBuy, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(buy_msg);
    EXPECT_TRUE(err.empty());

    free(buy_msg);
}

// 测试场景：批量报单成功，批量响应后订单进入 batch_orders_
TEST_F(AntiSelfKnockRiskTest, BatchOrderSuccess) {
    // 构造2个item的批量报单
    auto msg_id = x::UUID();
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(msg->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->items_size = 2;
    msg->timestamp = x::UnixMilli();
    FillOrderItem(&msg->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);
    FillOrderItem(&msg->items[1], "600001.SH", kBsFlagSell, 9.98, 1000);

    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg).empty());
    risk_.OnTradeOrderReqPass(msg);

    // 构造批量响应（batch_no 不为空）
    auto rep_id = x::UUID();
    auto rep = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(rep, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(rep->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(rep->fund_id, "fund_a", kMemFundIdSize);
    strncpy(rep->batch_no, "batch_001", kMemBatchNoSize);
    rep->items_size = 2;
    rep->timestamp = x::UnixMilli();
    FillOrderItem(&rep->items[0], "600000.SH", kBsFlagSell, 9.98, 1000, "order_001");
    FillOrderItem(&rep->items[1], "600001.SH", kBsFlagSell, 9.98, 1000, "order_002");

    risk_.HandleTradeOrderRep(rep);

    // 验证 batch_orders_ 中有记录
    auto bit = risk_.batch_orders_.find("batch_001");
    ASSERT_NE(bit, risk_.batch_orders_.end());
    EXPECT_EQ(bit->second->size(), 2);

    // 验证 single_orders_ 中也有记录
    EXPECT_NE(risk_.single_orders_.find("order_001"), risk_.single_orders_.end());
    EXPECT_NE(risk_.single_orders_.find("order_002"), risk_.single_orders_.end());

    free(msg);
    free(rep);
}

// 测试场景：批量报单，一笔成功一笔失败，失败 item 设 error
TEST_F(AntiSelfKnockRiskTest, BatchOrderFail) {
    auto msg_id = x::UUID();
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(msg->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->items_size = 2;
    msg->timestamp = x::UnixMilli();
    FillOrderItem(&msg->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);
    FillOrderItem(&msg->items[1], "600001.SH", kBsFlagSell, 9.98, 1000);

    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg).empty());
    risk_.OnTradeOrderReqPass(msg);

    // 验证两笔卖单都入了订单簿
    auto book_600000 = risk_.MustGetOrderBook("600000.SH");
    auto book_600001 = risk_.MustGetOrderBook("600001.SH");
    EXPECT_EQ(book_600000->asks_.size(), 1);
    EXPECT_EQ(book_600001->asks_.size(), 1);

    // 构造响应：一条成功（有 order_no），一条失败（order_no 为空，设 error）
    auto rep = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(rep, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(rep->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(rep->fund_id, "fund_a", kMemFundIdSize);
    rep->items_size = 2;
    rep->timestamp = x::UnixMilli();
    // 第一笔成功
    FillOrderItem(&rep->items[0], "600000.SH", kBsFlagSell, 9.98, 1000, "order_001");
    // 第二笔失败（无 order_no）
    FillOrderItem(&rep->items[1], "600001.SH", kBsFlagSell, 9.98, 1000);
    strncpy(rep->items[1].error, "超过持仓限制", kMemErrorSize);

    risk_.HandleTradeOrderRep(rep);

    // 成功的订单保留在 single_orders_ 中
    EXPECT_NE(risk_.single_orders_.find("order_001"), risk_.single_orders_.end());

    // 失败的单从订单簿移除
    EXPECT_EQ(book_600001->asks_.size(), 0);

    // 再报第一笔反方向买单，与 order_001（卖 600000 9.98）交叉，检测到对敲
    auto buy_msg_id = x::UUID();
    auto buy_msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(buy_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(buy_msg->id, buy_msg_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg->fund_id, "fund_a", kMemFundIdSize);
    buy_msg->items_size = 1;
    buy_msg->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(buy_msg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);

    // 再报第二笔反方向买单，代码 600003，不同证券无交叉，成功通过
    auto buy_msg2_id = x::UUID();
    auto buy_msg2 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(buy_msg2, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(buy_msg2->id, buy_msg2_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg2->fund_id, "fund_a", kMemFundIdSize);
    buy_msg2->items_size = 1;
    buy_msg2->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg2->items[0], "600003.SH", kBsFlagBuy, 9.98, 1000);

    err = risk_.HandleTradeOrderReq(buy_msg2);
    EXPECT_TRUE(err.empty());

    free(msg);
    free(rep);
    free(buy_msg);
    free(buy_msg2);
}

// 测试场景：批量报买单，两笔成功一笔失败，失败设 error；
// 部分成交和分笔成交先到，报单响应后到处理；
// 再报单笔卖单被对敲检测拒绝
TEST_F(AntiSelfKnockRiskTest, ComplexBatchBuyWithKnockBeforeRep) {
    // 1. 构造批量买单（3个item）
    auto msg_id = x::UUID();
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 3);
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 3);
    strncpy(msg->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->items_size = 3;
    msg->timestamp = x::UnixMilli();
    FillOrderItem(&msg->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000);
    FillOrderItem(&msg->items[1], "600001.SH", kBsFlagBuy, 9.98, 1000);
    FillOrderItem(&msg->items[2], "600002.SH", kBsFlagBuy, 9.98, 1000);

    EXPECT_TRUE(risk_.HandleTradeOrderReq(msg).empty());
    risk_.OnTradeOrderReqPass(msg);

    // 验证三个 item 都入了订单簿
    auto book_600000 = risk_.MustGetOrderBook("600000.SH");
    auto book_600001 = risk_.MustGetOrderBook("600001.SH");
    auto book_600002 = risk_.MustGetOrderBook("600002.SH");
    EXPECT_EQ(book_600000->bids_.size(), 1);
    EXPECT_EQ(book_600001->bids_.size(), 1);
    EXPECT_EQ(book_600002->bids_.size(), 1);

    // 2. 响应到达前成交先到
    // order_001 部分成交 300
    MemTradeKnock knock1;
    memset(&knock1, 0, sizeof(knock1));
    strncpy(knock1.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock1.batch_no, "batch_001", kMemBatchNoSize);
    strncpy(knock1.order_no, "order_001", kMemOrderNoSize);
    strncpy(knock1.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock1.code, "600000.SH", kMemCodeSize);
    knock1.match_type = kMatchTypeOK;
    knock1.match_volume = 300;
    knock1.timestamp = x::UnixMilli();
    risk_.OnTradeKnock(&knock1);

    // order_002 分笔成交 400 + 600
    MemTradeKnock knock2a, knock2b;
    memset(&knock2a, 0, sizeof(knock2a));
    strncpy(knock2a.match_no, "match_002a", kMemMatchNoSize);
    strncpy(knock2a.batch_no, "batch_001", kMemBatchNoSize);
    strncpy(knock2a.order_no, "order_002", kMemOrderNoSize);
    strncpy(knock2a.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock2a.code, "600001.SH", kMemCodeSize);
    knock2a.match_type = kMatchTypeOK;
    knock2a.match_volume = 400;
    knock2a.timestamp = x::UnixMilli();
    risk_.OnTradeKnock(&knock2a);

    memset(&knock2b, 0, sizeof(knock2b));
    strncpy(knock2b.match_no, "match_002b", kMemMatchNoSize);
    strncpy(knock2b.batch_no, "batch_001", kMemBatchNoSize);
    strncpy(knock2b.order_no, "order_002", kMemOrderNoSize);
    strncpy(knock2b.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock2b.code, "600001.SH", kMemCodeSize);
    knock2b.match_type = kMatchTypeOK;
    knock2b.match_volume = 600;
    knock2b.timestamp = x::UnixMilli();
    risk_.OnTradeKnock(&knock2b);

    // 验证成交缓存
    EXPECT_EQ(risk_.knock_first_orders_.size(), 2);
    EXPECT_EQ(risk_.knock_first_orders_["order_001"]->size(), 1);
    EXPECT_EQ(risk_.knock_first_orders_["order_002"]->size(), 2);

    // 3. 报单响应到达
    auto rep = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 3);
    memset(rep, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 3);
    strncpy(rep->id, msg_id.c_str(), kMemIdSize - 1);
    strncpy(rep->fund_id, "fund_a", kMemFundIdSize);
    strncpy(rep->batch_no, "batch_001", kMemBatchNoSize);
    rep->items_size = 3;
    rep->timestamp = x::UnixMilli();
    FillOrderItem(&rep->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000, "order_001");
    FillOrderItem(&rep->items[1], "600001.SH", kBsFlagBuy, 9.98, 1000, "order_002");
    FillOrderItem(&rep->items[2], "600002.SH", kBsFlagBuy, 9.98, 1000);
    strncpy(rep->items[2].error, "超过持仓限制", kMemErrorSize);

    risk_.HandleTradeOrderRep(rep);

    // 缓存已消费
    EXPECT_TRUE(risk_.knock_first_orders_.empty());

    // order_001 部分成交 300，仍在单中
    auto it1 = risk_.single_orders_.find("order_001");
    ASSERT_NE(it1, risk_.single_orders_.end());
    EXPECT_EQ(it1->second->match_volume, 300);

    // order_002 全部成交 400+600=1000，已删除
    EXPECT_EQ(risk_.single_orders_.find("order_002"), risk_.single_orders_.end());

    // 失败 item 从订单簿移除
    EXPECT_EQ(book_600002->bids_.size(), 0);

    // batch_orders_ 有 2 条记录
    auto bit = risk_.batch_orders_.find("batch_001");
    ASSERT_NE(bit, risk_.batch_orders_.end());
    EXPECT_EQ(bit->second->size(), 2);

    // 4. 再报单笔卖单失败：同 fund_id 卖 600000.SH 9.98，与 order_001 对敲
    auto sell_msg_id = x::UUID();
    auto sell_msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(sell_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(sell_msg->id, sell_msg_id.c_str(), kMemIdSize - 1);
    strncpy(sell_msg->fund_id, "fund_a", kMemFundIdSize);
    sell_msg->items_size = 1;
    sell_msg->timestamp = x::UnixMilli();
    FillOrderItem(&sell_msg->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(sell_msg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);

    // 5. 报单 600001.SH 卖单，与 order_002（已全部成交删除）方向相反，应通过
    auto sell_msg2_id = x::UUID();
    auto sell_msg2 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(sell_msg2, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(sell_msg2->id, sell_msg2_id.c_str(), kMemIdSize - 1);
    strncpy(sell_msg2->fund_id, "fund_a", kMemFundIdSize);
    sell_msg2->items_size = 1;
    sell_msg2->timestamp = x::UnixMilli();
    FillOrderItem(&sell_msg2->items[0], "600001.SH", kBsFlagSell, 9.98, 1000);

    err = risk_.HandleTradeOrderReq(sell_msg2);
    EXPECT_TRUE(err.empty());

    // 第三笔反方向报单：卖 600002.SH，该 item 之前报单失败无订单，应通过
    auto sell_msg3_id = x::UUID();
    auto sell_msg3 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(sell_msg3, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(sell_msg3->id, sell_msg3_id.c_str(), kMemIdSize - 1);
    strncpy(sell_msg3->fund_id, "fund_a", kMemFundIdSize);
    sell_msg3->items_size = 1;
    sell_msg3->timestamp = x::UnixMilli();
    FillOrderItem(&sell_msg3->items[0], "600002.SH", kBsFlagSell, 9.98, 1000);

    err = risk_.HandleTradeOrderReq(sell_msg3);
    EXPECT_TRUE(err.empty());

    // 6. order_001 收到 700 成交回报，累计 300+700=1000 全部成交，订单删除
    MemTradeKnock knock_final;
    memset(&knock_final, 0, sizeof(knock_final));
    strncpy(knock_final.match_no, "match_final", kMemMatchNoSize);
    strncpy(knock_final.batch_no, "batch_001", kMemBatchNoSize);
    strncpy(knock_final.order_no, "order_001", kMemOrderNoSize);
    strncpy(knock_final.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock_final.code, "600000.SH", kMemCodeSize);
    knock_final.match_type = kMatchTypeOK;
    knock_final.match_volume = 700;
    knock_final.timestamp = x::UnixMilli();
    risk_.OnTradeKnock(&knock_final);

    // order_001 已全部成交，从 single_orders_ 删除
    EXPECT_EQ(risk_.single_orders_.find("order_001"), risk_.single_orders_.end());

    // 再报第 4 步的卖单（600000.SH 9.98），不再对敲，成功通过
    err = risk_.HandleTradeOrderReq(sell_msg);
    EXPECT_TRUE(err.empty());

    // batch_orders_ 中 batch_001 已无活跃订单，应被清理
    EXPECT_EQ(risk_.batch_orders_.find("batch_001"), risk_.batch_orders_.end());

    free(msg);
    free(rep);
    free(sell_msg);
    free(sell_msg2);
    free(sell_msg3);
}

// 测试场景：单笔买单（不给响应），报批量卖单包含同代码失败，超时后再报通过
TEST_F(AntiSelfKnockRiskTest, TimeoutThenBatchSellPass) {
    // 1. 单笔买单 600000.SH，不给响应
    auto buy_id = x::UUID();
    auto buy_msg = (MemTradeOrderMessage*)malloc(sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(buy_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(buy_msg->id, buy_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg->fund_id, "fund_a", kMemFundIdSize);
    buy_msg->items_size = 1;
    buy_msg->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000);

    EXPECT_TRUE(risk_.HandleTradeOrderReq(buy_msg).empty());
    risk_.OnTradeOrderReqPass(buy_msg);

    // 验证买单已入 bids_
    auto book_600000 = risk_.MustGetOrderBook("600000.SH");
    EXPECT_EQ(book_600000->bids_.size(), 1);

    // 2. 批量卖单 600000.SH + 600001.SH，因同 fund_id 对敲失败
    auto sell_id = x::UUID();
    auto sell_msg = (MemTradeOrderMessage*)malloc(sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(sell_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(sell_msg->id, sell_id.c_str(), kMemIdSize - 1);
    strncpy(sell_msg->fund_id, "fund_a", kMemFundIdSize);
    sell_msg->items_size = 2;
    sell_msg->timestamp = x::UnixMilli();
    FillOrderItem(&sell_msg->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);
    FillOrderItem(&sell_msg->items[1], "600001.SH", kBsFlagSell, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(sell_msg);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("防对敲"), std::string::npos);

    x::Sleep(3500);

    // 4. 再报批量卖单，买单已超时被清理，应通过
    err = risk_.HandleTradeOrderReq(sell_msg);
    EXPECT_TRUE(err.empty());

    // 超时买单已被清理
    EXPECT_EQ(book_600000->bids_.size(), 0);

    free(buy_msg);
    free(sell_msg);
}

// 测试场景：批量买单，给响应；反方向失败；撤单（无响应）；反方向仍失败；超时后再报通过
TEST_F(AntiSelfKnockRiskTest, BatchBuyWithWithdrawThenTimeout) {
    // 1. 批量两笔买单 600000.SH + 600001.SH
    auto buy_id = x::UUID();
    auto buy_msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(buy_msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(buy_msg->id, buy_id.c_str(), kMemIdSize - 1);
    strncpy(buy_msg->fund_id, "fund_a", kMemFundIdSize);
    buy_msg->items_size = 2;
    buy_msg->timestamp = x::UnixMilli();
    FillOrderItem(&buy_msg->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000);
    FillOrderItem(&buy_msg->items[1], "600001.SH", kBsFlagBuy, 9.98, 1000);

    EXPECT_TRUE(risk_.HandleTradeOrderReq(buy_msg).empty());
    risk_.OnTradeOrderReqPass(buy_msg);

    // 给响应（带 batch_no）
    auto rep = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(rep, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(rep->id, buy_id.c_str(), kMemIdSize - 1);
    strncpy(rep->fund_id, "fund_a", kMemFundIdSize);
    strncpy(rep->batch_no, "batch_002", kMemBatchNoSize);
    rep->items_size = 2;
    rep->timestamp = x::UnixMilli();
    FillOrderItem(&rep->items[0], "600000.SH", kBsFlagBuy, 9.98, 1000, "order_001");
    FillOrderItem(&rep->items[1], "600001.SH", kBsFlagBuy, 9.98, 1000, "order_002");

    risk_.HandleTradeOrderRep(rep);

    // 2. 反方向单笔卖单（600000.SH）失败
    auto sell1_id = x::UUID();
    auto sell1 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(sell1, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(sell1->id, sell1_id.c_str(), kMemIdSize - 1);
    strncpy(sell1->fund_id, "fund_a", kMemFundIdSize);
    sell1->items_size = 1;
    sell1->timestamp = x::UnixMilli();
    FillOrderItem(&sell1->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);

    std::string err = risk_.HandleTradeOrderReq(sell1);
    EXPECT_FALSE(err.empty());

    // 反方向批量卖单（两笔都含同 fund_id）也失败
    auto sell2_id = x::UUID();
    auto sell2 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    memset(sell2, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * 2);
    strncpy(sell2->id, sell2_id.c_str(), kMemIdSize - 1);
    strncpy(sell2->fund_id, "fund_a", kMemFundIdSize);
    sell2->items_size = 2;
    sell2->timestamp = x::UnixMilli();
    FillOrderItem(&sell2->items[0], "600000.SH", kBsFlagSell, 9.98, 1000);
    FillOrderItem(&sell2->items[1], "600001.SH", kBsFlagSell, 9.98, 1000);

    err = risk_.HandleTradeOrderReq(sell2);
    EXPECT_FALSE(err.empty());

    // 3. 撤单（只发请求，不给响应），按 batch_no 撤单
    MemTradeWithdrawMessage withdraw;
    memset(&withdraw, 0, sizeof(withdraw));
    strncpy(withdraw.id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(withdraw.fund_id, "fund_a", kMemFundIdSize);
    strncpy(withdraw.batch_no, "batch_002", kMemBatchNoSize);
    withdraw.timestamp = x::UnixMilli();
    risk_.HandleTradeWithdrawReq(&withdraw);

    // 4. 反方向仍失败（withdraw_time 刚设，未超 3s）
    err = risk_.HandleTradeOrderReq(sell1);
    EXPECT_FALSE(err.empty());

    err = risk_.HandleTradeOrderReq(sell2);
    EXPECT_FALSE(err.empty());

    // 5. 等待超时
    x::Sleep(3500);

    // 再报反方向，买单已超时被清理，通过
    err = risk_.HandleTradeOrderReq(sell1);
    EXPECT_TRUE(err.empty());

    err = risk_.HandleTradeOrderReq(sell2);
    EXPECT_TRUE(err.empty());

    auto book_600000 = risk_.MustGetOrderBook("600000.SH");
    EXPECT_EQ(book_600000->bids_.size(), 0);

    free(buy_msg);
    free(rep);
    free(sell1);
    free(sell2);
}
}  // namespace co
