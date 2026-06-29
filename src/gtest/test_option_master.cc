// Copyright 2026 Fancapital Inc.  All rights reserved.
#include <gtest/gtest.h>
#include <cstring>

#define private public
#include "../mem_broker/position_master.h"
#undef private

namespace co {

class OptionPositionTest : public ::testing::Test {
protected:
    void SetUp() override {
        master_ = std::make_unique<PositionMaster>(kTradeTypeOption);
    }

    void AddInitPosition(const char* code, int64_t long_can_close, int64_t short_can_close) {
        MemTradePosition pos;
        memset(&pos, 0, sizeof(pos));
        strncpy(pos.code, code, kMemCodeSize);
        pos.long_can_close = long_can_close;
        pos.short_can_close = short_can_close;
        master_->InitPositions(&pos, false);
    }

    std::unique_ptr<PositionMaster> master_;
};

// 测试场景：InitPositions 初始化期权持仓（多头+空头）
TEST_F(OptionPositionTest, InitPosition) {
    AddInitPosition("10003984.SH", 3000, 2000);

    auto long_pos = master_->GetPosition("10003984.SH", kBsFlagBuy, kOcFlagOpen);
    ASSERT_NE(long_pos, nullptr);
    EXPECT_EQ(long_pos->td_init_volume_, 3000);
    EXPECT_EQ(long_pos->yd_init_volume_, 0);

    auto short_pos = master_->GetPosition("10003984.SH", kBsFlagSell, kOcFlagOpen);
    ASSERT_NE(short_pos, nullptr);
    EXPECT_EQ(short_pos->td_init_volume_, 2000);
    EXPECT_EQ(short_pos->yd_init_volume_, 0);
}

// 测试场景：HandleOrderReq 开仓委托
TEST_F(OptionPositionTest, OrderReqOpen) {
    AddInitPosition("10003984.SH", 3000, 2000);

    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagBuy;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 5000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagOpen);

    master_->HandleOrderReq(msg);

    auto pos = master_->GetPosition("10003984.SH", kBsFlagBuy, kOcFlagOpen);
    ASSERT_NE(pos, nullptr);
    // 开仓委托冻结
    EXPECT_EQ(pos->td_opening_volume_, 5000);
    EXPECT_EQ(pos->td_open_volume_, 0);
    EXPECT_EQ(pos->td_init_volume_, 3000);

    free(msg);
}

// 测试场景：HandleOrderReq 平仓委托（有足够持仓）
TEST_F(OptionPositionTest, OrderReqClose) {
    AddInitPosition("10003984.SH", 3000, 2000);

    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagClose);

    master_->HandleOrderReq(msg);

    auto pos = master_->GetPosition("10003984.SH", msg->bs_flag, msg->items[0].oc_flag);
    ASSERT_NE(pos, nullptr);
    // 卖出平仓，冻结在多头持仓上
    EXPECT_EQ(pos->td_closing_volume_, 1000);
    EXPECT_EQ(pos->td_close_volume_, 0);

    free(msg);
}

// 测试场景：HandleOrderRep 废单（解冻）
TEST_F(OptionPositionTest, OrderRepFail) {
    AddInitPosition("10003984.SH", 3000, 2000);

    // 先发一笔平仓委托
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagClose);

    master_->HandleOrderReq(msg);

    auto pos = master_->GetPosition("10003984.SH", kBsFlagSell, kOcFlagClose);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->td_closing_volume_, 1000);

    // 废单响应
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    master_->HandleOrderRep(msg);

    EXPECT_EQ(pos->td_closing_volume_, 0);

    free(msg);
}

// 测试场景：HandleKnock 开仓成交
TEST_F(OptionPositionTest, KnockOpenMatch) {
    AddInitPosition("10003984.SH", 3000, 2000);

    // 先发开仓委托
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagBuy;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 5000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagOpen);

    master_->HandleOrderReq(msg);

    // 成交
    MemTradeKnock knock;
    memset(&knock, 0, sizeof(knock));
    strncpy(knock.code, "10003984.SH", kMemCodeSize);
    strncpy(knock.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock.order_no, "order_001", kMemOrderNoSize);
    knock.bs_flag = kBsFlagBuy;
    knock.oc_flag = kOcFlagOpen;
    knock.match_type = kMatchTypeOK;
    knock.match_volume = 5000;
    knock.timestamp = x::UnixMilli();
    master_->HandleKnock(knock);

    auto pos = master_->GetPosition("10003984.SH", kBsFlagBuy, kOcFlagOpen);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->td_opening_volume_, 0);
    EXPECT_EQ(pos->td_open_volume_, 5000);

    free(msg);
}

// 测试场景：HandleKnock 平仓成交
TEST_F(OptionPositionTest, KnockCloseMatch) {
    AddInitPosition("10003984.SH", 3000, 2000);

    // 先发平仓委托
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagSell;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagClose);

    master_->HandleOrderReq(msg);

    // 成交
    MemTradeKnock knock;
    memset(&knock, 0, sizeof(knock));
    strncpy(knock.code, "10003984.SH", kMemCodeSize);
    strncpy(knock.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock.order_no, "order_001", kMemOrderNoSize);
    knock.bs_flag = kBsFlagSell;
    knock.oc_flag = kOcFlagClose;
    knock.match_type = kMatchTypeOK;
    knock.match_volume = 1000;
    knock.timestamp = x::UnixMilli();
    master_->HandleKnock(knock);

    auto pos = master_->GetPosition("10003984.SH", kBsFlagSell, kOcFlagClose);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->td_closing_volume_, 0);
    EXPECT_EQ(pos->td_close_volume_, 1000);

    free(msg);
}

// 测试场景：分批成交
TEST_F(OptionPositionTest, PartialMatch) {
    AddInitPosition("10003984.SH", 3000, 2000);

    // 开仓委托
    auto msg = (MemTradeOrderMessage*)malloc(
        sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    memset(msg, 0, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder));
    strncpy(msg->id, x::UUID().c_str(), kMemIdSize - 1);
    strncpy(msg->fund_id, "fund_a", kMemFundIdSize);
    msg->bs_flag = kBsFlagBuy;
    msg->items_size = 1;
    msg->timestamp = x::UnixMilli();
    strncpy(msg->items[0].code, "10003984.SH", kMemCodeSize);
    msg->items[0].volume = 1000;
    msg->items[0].price = 1.5;
    msg->items[0].timestamp = x::UnixMilli();
    msg->items[0].oc_flag = master_->GetAutoOcFlag(msg->bs_flag, msg->items[0]);
    EXPECT_EQ(msg->items[0].oc_flag, kOcFlagClose);
    master_->HandleOrderReq(msg);

    auto pos = master_->GetPosition("10003984.SH", msg->bs_flag, msg->items[0].oc_flag);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->td_closing_volume_, 1000);

    // 部分成交 300
    MemTradeKnock knock1;
    memset(&knock1, 0, sizeof(knock1));
    strncpy(knock1.code, "10003984.SH", kMemCodeSize);
    strncpy(knock1.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock1.match_no, "match_001", kMemMatchNoSize);
    strncpy(knock1.order_no, "order_001", kMemOrderNoSize);
    knock1.bs_flag = kBsFlagBuy;
    knock1.oc_flag = msg->items[0].oc_flag;
    knock1.match_type = kMatchTypeOK;
    knock1.match_volume = 300;
    knock1.timestamp = x::UnixMilli();
    master_->HandleKnock(knock1);

    EXPECT_EQ(pos->td_closing_volume_, 700);
    EXPECT_EQ(pos->td_close_volume_, 300);

    // 再成交 700
    MemTradeKnock knock2;
    memset(&knock2, 0, sizeof(knock2));
    strncpy(knock2.code, "10003984.SH", kMemCodeSize);
    strncpy(knock2.fund_id, "fund_a", kMemFundIdSize);
    strncpy(knock2.match_no, "match_002", kMemMatchNoSize);
    strncpy(knock2.order_no, "order_001", kMemOrderNoSize);
    knock2.bs_flag = kBsFlagBuy;
    knock2.oc_flag = msg->items[0].oc_flag;
    knock2.match_type = kMatchTypeOK;
    knock2.match_volume = 700;
    knock2.timestamp = x::UnixMilli();
    master_->HandleKnock(knock2);

    EXPECT_EQ(pos->td_closing_volume_, 0);
    EXPECT_EQ(pos->td_close_volume_, 1000);

    free(msg);
}

// ============================================================
// GetAutoOcFlag 测试 — 期权 (kTradeTypeOption)
// ============================================================

// 今日持仓 > 委托量 + 卖方向 → kOcFlagClose
TEST_F(OptionPositionTest, GetAutoOcFlagCloseWithSufficientTd) {
    AddInitPosition("10003984.SH", 3000, 2000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "10003984.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 1.5;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagClose);
}

// 今日持仓 < 委托量 + 卖方向 → kOcFlagOpen（默认）
TEST_F(OptionPositionTest, GetAutoOcFlagCloseWithInsufficientTd) {
    AddInitPosition("10003984.SH", 500, 2000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "10003984.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 1.5;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

// 今日持仓 == 委托量 + 卖方向 → kOcFlagOpen（严格大于）
TEST_F(OptionPositionTest, GetAutoOcFlagCloseWithEqualTd) {
    AddInitPosition("10003984.SH", 1000, 2000);

    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "10003984.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 1.5;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

// 无持仓 + 卖方向 → kOcFlagOpen
TEST_F(OptionPositionTest, GetAutoOcFlagNoPosition) {
    MemTradeOrder order;
    memset(&order, 0, sizeof(order));
    strncpy(order.code, "10003984.SH", kMemCodeSize);
    order.bs_flag = kBsFlagSell;
    order.volume = 1000;
    order.price = 1.5;
    order.market = 0;

    int64_t oc_flag = master_->GetAutoOcFlag(kBsFlagSell, order);
    EXPECT_EQ(oc_flag, kOcFlagOpen);
}

}  // namespace co
