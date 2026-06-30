// Copyright 2026 Fancapital Inc.  All rights reserved.
#include <gtest/gtest.h>
#include <cstring>

#define private public
#include "../mem_broker/position_master.h"
#undef private

namespace co {

// ============================================================
// PositionMaster Credit 交易测试
// ============================================================

class CreditPositionTest : public ::testing::Test {
protected:
    void SetUp() override {
        master_ = std::make_unique<PositionMaster>(kTradeTypeCredit);
    }

    void AddInitPosition(const char* code, int64_t long_can_close) {
        MemTradePosition pos;
        memset(&pos, 0, sizeof(pos));
        strncpy(pos.code, code, kMemCodeSize);
        pos.long_can_close = long_can_close;
        master_->InitPositions(&pos, false);
    }

    std::unique_ptr<PositionMaster> master_;
};

//// 测试场景：InitPositions 初始化信用持仓
TEST_F(CreditPositionTest, InitPosition) {
    AddInitPosition("600000.SH", 5000);

    auto pos = master_->GetPosition("600000.SH", kBsFlagSell, kOcFlagAuto);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->yd_init_volume_, 5000);
    EXPECT_EQ(pos->td_init_volume_, 0);
    EXPECT_EQ(pos->yd_closing_volume_, 0);
    EXPECT_EQ(pos->yd_close_volume_, 0);
}

// 测试场景：GetPosition 反方向（买单）返回 nullptr
TEST_F(CreditPositionTest, GetPositionBuyReturnsNull) {
    AddInitPosition("600000.SH", 5000);

    auto pos = master_->GetPosition("600000.SH", kBsFlagBuy, kOcFlagOpen);
    EXPECT_EQ(pos, nullptr);
}

// 测试场景：HandleTradeOrderReq 卖单委托，冻结数量
TEST_F(CreditPositionTest, OrderReqSell) {
    AddInitPosition("600000.SH", 5000);

    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "600000.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 9.98;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagAuto);

        master_->HandleTradeOrderReq(msg);

    auto pos = master_->GetPosition("600000.SH", kBsFlagSell, kOcFlagAuto);
    ASSERT_NE(pos, nullptr);
    // 担保品卖出，冻结 yd_closing_volume_
    EXPECT_EQ(pos->yd_closing_volume_, 1000);
    EXPECT_EQ(pos->yd_close_volume_, 0);
    EXPECT_EQ(pos->yd_init_volume_, 5000);

    // 再发一笔 4500 的卖单
    auto msg2 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg2, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg2->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg2->fund_id, "fund_a", kMemFundIdSize);
    msg2->bs_flag = kBsFlagSell;
    msg2->items_size = 1;
    msg2->timestamp = x::UnixMilli();
    strncpy(msg2->items[0].code, "600000.SH", kMemCodeSize);
    msg2->items[0].volume = 4500;
    msg2->items[0].price = 9.98;
    msg2->items[0].timestamp = x::UnixMilli();
    msg2->items[0].oc_flag = master_->GetAutoOcFlag(msg2->bs_flag, msg2->items[0]);
    EXPECT_EQ(msg2->items[0].oc_flag, kOcFlagOpen);
        master_->HandleTradeOrderReq(msg2);

    auto pos2 = master_->GetPosition("600000.SH", kBsFlagSell, msg2->items[0].oc_flag);
    ASSERT_EQ(pos2, nullptr);

    free(msg2);
    free(msg);
}

// 测试场景：HandleTradeOrderRep 卖单失败，解冻数量
TEST_F(CreditPositionTest, OrderRepFailSell) {
    AddInitPosition("600000.SH", 5000);

    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "600000.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 9.98;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagAuto);

    // 先发委托
        master_->HandleTradeOrderReq(msg);

    auto pos = master_->GetPosition("600000.SH", kBsFlagSell, kOcFlagAuto);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->yd_closing_volume_, 1000);

    // 废单响应（order_no 为空）
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    // order_no 默认空字符串
    master_->HandleTradeOrderRep(msg);

    // 解冻
    EXPECT_EQ(pos->yd_closing_volume_, 0);

    free(msg);
}

// 测试场景：HandleTradeKnock 卖单成交
TEST_F(CreditPositionTest, KnockSellMatch) {
    AddInitPosition("600000.SH", 5000);

    // 先委托冻结
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "600000.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 9.98;
    msg->items[0].timestamp = x::UnixMilli();
        master_->HandleTradeOrderReq(msg);

    // 成交回报
    MemTradeKnock knock;
    memset(&knock, 0, sizeof(knock));
    strncpy(knock.code, "600000.SH", kMemCodeSize);
    strncpy(knock.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock.order_no, "order_001", kMemOrderNoSize);
    knock.bs_flag = kBsFlagSell;
    knock.oc_flag = kOcFlagAuto;
    knock.match_type = kMatchTypeOK;
    knock.match_volume = 1000;
    knock.timestamp = x::UnixMilli();
    master_->HandleTradeKnock(knock);

    auto pos = master_->GetPosition("600000.SH", kBsFlagSell, kOcFlagAuto);
    ASSERT_NE(pos, nullptr);
    // 冻结减少，已平仓增加
    EXPECT_EQ(pos->yd_closing_volume_, 0);
    EXPECT_EQ(pos->yd_close_volume_, 1000);
    EXPECT_EQ(pos->yd_init_volume_, 5000);

    free(msg);
}

// 测试场景：多笔卖单，分批成交
TEST_F(CreditPositionTest, MultipleSellsPartialMatch) {
    AddInitPosition("600000.SH", 5000);

    // 第一笔卖单委托
    auto msg1 = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg1, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg1->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg1->fund_id, "fund_a", kMemFundIdSize);
    msg1->bs_flag = kBsFlagSell;
    msg1->items_size = 1;
    msg1->timestamp = x::UnixMilli();
    strncpy(msg1->items[0].code, "600000.SH", kMemCodeSize);
    msg1->items[0].volume = 2000;
    msg1->items[0].price = 9.98;
    msg1->items[0].timestamp = x::UnixMilli();
    msg1->items[0].oc_flag = master_->GetAutoOcFlag(msg1->bs_flag, msg1->items[0]);
    EXPECT_EQ(msg1->items[0].oc_flag, kOcFlagAuto);
        master_->HandleTradeOrderReq(msg1);

    auto pos = master_->GetPosition("600000.SH", kBsFlagSell, kOcFlagAuto);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->yd_closing_volume_, 2000);

    // 部分成交 800
    MemTradeKnock knock1;
    memset(&knock1, 0, sizeof(knock1));
    strncpy(knock1.code, "600000.SH", kMemCodeSize);
    strncpy(knock1.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock1.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock1.order_no, "order_001", kMemOrderNoSize);
    knock1.bs_flag = kBsFlagSell;
    knock1.oc_flag = kOcFlagAuto;
    knock1.match_type = kMatchTypeOK;
    knock1.match_volume = 800;
    knock1.timestamp = x::UnixMilli();
    master_->HandleTradeKnock(knock1);

    EXPECT_EQ(pos->yd_closing_volume_, 1200);
    EXPECT_EQ(pos->yd_close_volume_, 800);

    // 再成交 1200
    MemTradeKnock knock2;
    memset(&knock2, 0, sizeof(knock2));
    strncpy(knock2.code, "600000.SH", kMemCodeSize);
    strncpy(knock2.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock2.match_no, "match_002", kMemMatchNoSize);
    strncpy(knock2.order_no, "order_001", kMemOrderNoSize);
    knock2.bs_flag = kBsFlagSell;
    knock2.oc_flag = kOcFlagAuto;
    knock2.match_type = kMatchTypeOK;
    knock2.match_volume = 1200;
    knock2.timestamp = x::UnixMilli();
    master_->HandleTradeKnock(knock2);

    EXPECT_EQ(pos->yd_closing_volume_, 0);
    EXPECT_EQ(pos->yd_close_volume_, 2000);

    free(msg1);
}

// ============================================================
// GetAutoOcFlag 测试 — 信用交易 (kTradeTypeCredit)
// ============================================================

// 昨日持仓 > 委托量 + 卖方向 → kOcFlagAuto
TEST_F(CreditPositionTest, GetAutoOcFlagSellWithSufficientYd) {
    AddInitPosition("600000.SH", 5000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "600000.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 9.98;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagAuto);
}

// 昨日持仓 < 委托量 + 卖方向 → kOcFlagOpen（默认）
TEST_F(CreditPositionTest, GetAutoOcFlagSellWithInsufficientYd) {
    AddInitPosition("600000.SH", 500);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "600000.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 9.98;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

// 昨日持仓 == 委托量 + 卖方向 → kOcFlagAuto（条件要求严格大于）
TEST_F(CreditPositionTest, GetAutoOcFlagSellWithEqualYd) {
    AddInitPosition("600000.SH", 1000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "600000.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 9.98;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagAuto);
}

// 买方向 → kOcFlagOpen
TEST_F(CreditPositionTest, GetAutoOcFlagBuy) {
    AddInitPosition("600000.SH", 5000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "600000.SH", kMemCodeSize);
    order.bs_flag = kBsFlagBuy;
    order.volume = 1000;
    order.price = 9.98;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagBuy, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

// 无持仓 + 卖方向 → kOcFlagOpen（默认）
TEST_F(CreditPositionTest, GetAutoOcFlagNoPosition) {
    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "600000.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 9.98;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

}  // namespace co
