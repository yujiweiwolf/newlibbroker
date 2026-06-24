// Copyright 2026 Fancapital Inc.  All rights reserved.
#include <gtest/gtest.h>

#define private public
#include "../mem_broker/flow_control.h"
#undef private

namespace co {

// ============================================================
// FlowControlMarketQueue 测试
// ============================================================

class FlowControlMarketQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        state_.market = 1;
        state_.total_cmd_size = 0;
        state_.timestamp = 0;

        cfg_.set_market(1);
        cfg_.set_th_tps_limit(10);
        cfg_.set_th_daily_limit(100);
    }

    MemFlowControlStateItem state_;
    FlowControlConfig cfg_;
};

// ============================================================
// FlowControlMarketQueue 基础 Push 和 Pop 测试
// ============================================================
// 测试目标：验证 Push 入队后，GetMaxPriorityItem 和 TryPop 能正确返回优先级最高的元素，
// 以及队列为空时返回 nullptr。
TEST_F(FlowControlMarketQueueTest, PushAndPop) {
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256];
    auto item1 = std::make_shared<FlowControlItem>(100, 1000.0, 1, buf1);
    auto item2 = std::make_shared<FlowControlItem>(200, 500.0, 1, buf2);

    queue.Push(item1);
    queue.Push(item2);

    // GetMaxPriorityItem 应返回优先级最高的（order_amount 最大的）
    auto top = queue.GetMaxPriorityItem(1000);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_amount, 1000.0);
    EXPECT_EQ(top->timestamp, 100);

    // TryPop 应弹出优先级最高的
    auto popped = queue.TryPop(1000);
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->order_amount, 1000.0);
    EXPECT_EQ(popped->msg, buf1);

    // 剩余一个元素
    top = queue.GetMaxPriorityItem(1000);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->order_amount, 500.0);

    popped = queue.TryPop(1000);
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->msg, buf2);

    // 队列为空
    EXPECT_EQ(queue.GetMaxPriorityItem(1000), nullptr);
    EXPECT_EQ(queue.TryPop(1000), nullptr);
}

// ============================================================
// 按 order_amount 降序优先级测试
// ============================================================
// 测试目标：插入不同 order_amount 的元素，验证 TryPop 按金额降序弹出。
TEST_F(FlowControlMarketQueueTest, PriorityOrderByAmount) {
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256], buf3[256];
    auto item1 = std::make_shared<FlowControlItem>(100, 100.0, 1, buf1);
    auto item2 = std::make_shared<FlowControlItem>(200, 300.0, 1, buf2);
    auto item3 = std::make_shared<FlowControlItem>(300, 200.0, 1, buf3);

    queue.Push(item1);
    queue.Push(item2);
    queue.Push(item3);

    // 按 order_amount 降序: 300 > 200 > 100
    EXPECT_EQ(queue.TryPop(1000)->order_amount, 300.0);
    EXPECT_EQ(queue.TryPop(1000)->order_amount, 200.0);
    EXPECT_EQ(queue.TryPop(1000)->order_amount, 100.0);
}

// ============================================================
// 相同 order_amount 时按 timestamp 升序优先级测试
// ============================================================
// 测试目标：相同金额的元素，时间戳更小的先出队。
TEST_F(FlowControlMarketQueueTest, PrioritySameAmountEarlierTimestamp) {
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256];
    auto item1 = std::make_shared<FlowControlItem>(200, 1000.0, 1, buf1);
    auto item2 = std::make_shared<FlowControlItem>(100, 1000.0, 1, buf2);

    queue.Push(item1);
    queue.Push(item2);

    // timestamp 100 < 200，优先级更高
    EXPECT_EQ(queue.TryPop(1000)->timestamp, 100);
    EXPECT_EQ(queue.TryPop(1000)->timestamp, 200);
}

// ============================================================
// 撤单具有最高优先级测试
// ============================================================
// 测试目标：撤单的 order_amount = kFlowControlPriorityWithdraw，其值远大于普通报单，
// 验证撤单优先于报单出队。
TEST_F(FlowControlMarketQueueTest, WithdrawHasHighestPriority) {
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256];
    auto withdraw = std::make_shared<FlowControlItem>(100, kFlowControlPriorityWithdraw, 1, buf1);
    auto order = std::make_shared<FlowControlItem>(200, 1000.0, 1, buf2);

    queue.Push(order);
    queue.Push(withdraw);

    // 撤单的 order_amount 远大于普通报单
    EXPECT_EQ(queue.TryPop(1000)->order_amount, kFlowControlPriorityWithdraw);
    EXPECT_EQ(queue.TryPop(1000)->order_amount, 1000.0);
}

// ============================================================
// TPS 限流导致 GetMaxPriorityItem 被阻塞测试
// ============================================================
// 测试目标：sent_ns_queue_ 中已有 th_tps_limit 条记录时，
// GetMaxPriorityItem 应返回 nullptr；时间窗口过期后可恢复正常。
TEST_F(FlowControlMarketQueueTest, TpsLimitBlocks) {
    cfg_.set_th_tps_limit(2);
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256], buf3[256];
    auto item1 = std::make_shared<FlowControlItem>(100, 100.0, 1, buf1);
    auto item2 = std::make_shared<FlowControlItem>(200, 200.0, 1, buf2);
    auto item3 = std::make_shared<FlowControlItem>(300, 300.0, 1, buf3);

    queue.Push(item1);
    queue.Push(item2);
    queue.Push(item3);

    // 先弹出两个，填充 sent_ns_queue_ 到 2 条
    EXPECT_NE(queue.TryPop(1000), nullptr);
    EXPECT_NE(queue.TryPop(1000), nullptr);

    // sent 队列已满（2 条），加 item3 的 sub_order_size=1，共 3 > th_tps_limit=2
    EXPECT_EQ(queue.GetMaxPriorityItem(1000), nullptr);

    // 时间窗口过期后（now_dt > 1000 + kFlowControlWindowMS=2500），可以再取
    EXPECT_NE(queue.GetMaxPriorityItem(3000), nullptr);
}

// ============================================================
// TPS 限流与 sub_order_size 关系测试
// ============================================================
// 测试目标：sub_order_size 较大的消息会消耗更多 TPS 额度，
// GetMaxPriorityItem 会正确将 sent_ns_queue_ 大小 + sub_order_size 之和与 th_tps_limit 比较。
TEST_F(FlowControlMarketQueueTest, TpsLimitWithSubOrderSize) {
    cfg_.set_th_tps_limit(3);
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256], buf2[256];
    // sub_order_size = 3，sent 队列空，3 <= 3 可通过
    auto item1 = std::make_shared<FlowControlItem>(100, 100.0, 3, buf1);
    queue.Push(item1);
    EXPECT_NE(queue.GetMaxPriorityItem(1000), nullptr);

    // Pop 后 sent_ns_queue_ 填充 3 个时间戳
    queue.TryPop(1000);

    // 再加一个 sub_order_size = 1 的消息
    auto item2 = std::make_shared<FlowControlItem>(200, 200.0, 1, buf2);
    queue.Push(item2);

    // sent 队列 3 + item2 的 1 = 4 > 3，被限流
    EXPECT_EQ(queue.GetMaxPriorityItem(1000), nullptr);

    // 窗口过期后可取
    EXPECT_NE(queue.GetMaxPriorityItem(3000), nullptr);
}

// ============================================================
// state.total_cmd_size 更新测试
// ============================================================
// 测试目标：TryPop 后 state_->total_cmd_size 应正确累加 sub_order_size。
TEST_F(FlowControlMarketQueueTest, StateTotalCmdSizeUpdated) {
    FlowControlMarketQueue queue(&state_, &cfg_);

    char buf1[256];
    auto item = std::make_shared<FlowControlItem>(100, 100.0, 3, buf1);
    queue.Push(item);
    queue.TryPop(1000);

    // state 的 total_cmd_size 应增加 sub_order_size
    EXPECT_EQ(state_.total_cmd_size, 3);
}

// ============================================================
// FlowControlQueue Push / Pop 测试
// ============================================================

class FlowControlQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 4; ++i) {
            state_items_[i].market = i;
            state_items_[i].total_cmd_size = 0;
            state_items_[i].timestamp = 0;
        }

        cfgs_[1].set_market(1);
        cfgs_[1].set_th_tps_limit(10);
        cfgs_[2].set_market(2);
        cfgs_[2].set_th_tps_limit(10);
        cfgs_[3].set_market(3);
        cfgs_[3].set_th_tps_limit(10);

        for (int i = 1; i < 4; ++i) {
            queue_.market_to_queue_[i] = std::make_shared<FlowControlMarketQueue>(&state_items_[i], &cfgs_[i]);
        }
    }

    FlowControlQueue queue_;
    MemFlowControlStateItem state_items_[4];
    FlowControlConfig cfgs_[4];
};

// ============================================================
// 跨市场按最高优先级 Pop 测试
// ============================================================
// 测试目标：三个市场队列均有数据时，Pop 应选出 order_amount 最高的市场消息出队。
TEST_F(FlowControlQueueTest, PopHighestPriorityAcrossMarkets) {
    char buf1[256], buf2[256], buf3[256];
    queue_.market_to_queue_[1]->Push(std::make_shared<FlowControlItem>(100, 100.0, 1, buf1));
    queue_.market_to_queue_[2]->Push(std::make_shared<FlowControlItem>(200, 300.0, 1, buf2));
    queue_.market_to_queue_[3]->Push(std::make_shared<FlowControlItem>(300, 200.0, 1, buf3));

    EXPECT_EQ(queue_.Pop(), buf2);
    EXPECT_EQ(queue_.Pop(), buf3);
    EXPECT_EQ(queue_.Pop(), buf1);
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 普通队列 Pop 测试
// ============================================================
// 测试目标：通过 Push 入队非流控消息类型，验证 Pop 能从普通队列取出消息。
TEST_F(FlowControlQueueTest, PopNormalQueueViaPush) {
    MemFrameHeader header;
    header.type = 999999;
    header.body_length = 0;
    std::vector<char> msg(sizeof(MemFrameHeader), 0);
    memcpy(msg.data(), &header, sizeof(MemFrameHeader));

    queue_.Push(msg.data());
    EXPECT_EQ(queue_.Pop(), msg.data());
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 流控队列优先于普通队列 Pop 测试
// ============================================================
// 测试目标：同时有流控队列消息和普通队列消息时，Pop 应优先取出流控队列的消息。
TEST_F(FlowControlQueueTest, FlowControlPriorityOverNormal) {
    MemFrameHeader header;
    header.type = 999999;
    header.body_length = 0;
    std::vector<char> normal_msg(sizeof(MemFrameHeader), 0);
    memcpy(normal_msg.data(), &header, sizeof(MemFrameHeader));
    queue_.Push(normal_msg.data());

    char flow_buf[256];
    queue_.market_to_queue_[1]->Push(
        std::make_shared<FlowControlItem>(100, 1000.0, 1, flow_buf));

    EXPECT_EQ(queue_.Pop(), flow_buf);
    EXPECT_EQ(queue_.Pop(), normal_msg.data());
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 所有队列为空时 Pop 测试
// ============================================================
// 测试目标：所有流控队列和普通队列均为空时，Pop 返回 nullptr。
TEST_F(FlowControlQueueTest, PopAllEmpty) {
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// TPS 限流导致 Pop 受阻测试
// ============================================================
// 测试目标：市场队列 TPS 满后，Pop 无法取出消息（返回 nullptr），直到窗口过期。
TEST_F(FlowControlQueueTest, PopWithTpsLimitOnOneMarket) {
    cfgs_[1].set_th_tps_limit(1);
    queue_.market_to_queue_[1] = std::make_shared<FlowControlMarketQueue>(&state_items_[1], &cfgs_[1]);

    char buf1[256], buf2[256];
    queue_.market_to_queue_[1]->Push(std::make_shared<FlowControlItem>(100, 1000.0, 1, buf1));
    queue_.market_to_queue_[1]->Push(std::make_shared<FlowControlItem>(200, 500.0, 1, buf2));

    EXPECT_EQ(queue_.Pop(), buf1);
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 无流控队列时普通队列 Pop 测试
// ============================================================
// 测试目标：所有流控队列为 nullptr，普通队列有消息时能正常 Pop。
TEST_F(FlowControlQueueTest, PopNormalWhenNoFlowControlQueue) {
    queue_.market_to_queue_[1] = nullptr;
    queue_.market_to_queue_[2] = nullptr;
    queue_.market_to_queue_[3] = nullptr;

    MemFrameHeader header;
    header.type = 999999;
    header.body_length = 0;
    std::vector<char> msg(sizeof(MemFrameHeader), 0);
    memcpy(msg.data(), &header, sizeof(MemFrameHeader));
    queue_.Push(msg.data());

    EXPECT_EQ(queue_.Pop(), msg.data());
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 单市场 Pop 测试
// ============================================================
// 测试目标：只有一个市场队列有数据时，Pop 能正确取出消息，取完后返回 nullptr。
TEST_F(FlowControlQueueTest, PopFromSingleMarket) {
    char buf[256];
    queue_.market_to_queue_[1]->Push(
        std::make_shared<FlowControlItem>(100, 100.0, 1, buf));

    EXPECT_EQ(queue_.Pop(), buf);
    EXPECT_EQ(queue_.Pop(), nullptr);
}

// ============================================================
// 多市场混合撤单/申赎/批量报单优先级排序测试
// ============================================================
// 测试目标：三个市场同时存在撤单(300B)、申赎(200B)、批量报单(普通金额)时，
// Pop 按 order_amount 降序出队，且撤单 > 申赎 > 普通报单。
TEST_F(FlowControlQueueTest, PopMixedWithdrawCreateRedeemAndBatchOrders) {
    char withdraw_sh[256], withdraw_sz[256], redeem[256], batch_order[256], normal_order[256];

    // 市场1: 撤单（最高优先级）
    queue_.market_to_queue_[1]->Push(
        std::make_shared<FlowControlItem>(100, kFlowControlPriorityWithdraw, 1, withdraw_sh));
    // 市场2: 撤单（最高优先级，时间更晚）
    queue_.market_to_queue_[2]->Push(
        std::make_shared<FlowControlItem>(200, kFlowControlPriorityWithdraw, 1, withdraw_sz));
    // 市场3: 申赎（次高优先级）
    queue_.market_to_queue_[3]->Push(
        std::make_shared<FlowControlItem>(150, kFlowControlPriorityCreateRedeem, 1, redeem));
    // 市场1: 批量报单（5个子单）
    queue_.market_to_queue_[1]->Push(
        std::make_shared<FlowControlItem>(300, 5000.0, 5, batch_order));
    // 市场2: 普通报单
    queue_.market_to_queue_[2]->Push(
        std::make_shared<FlowControlItem>(400, 1000.0, 1, normal_order));

    // 出队顺序：撤单(market1, timestamp=100) > 撤单(market2, timestamp=200) > 申赎 > 批量报单 > 普通报单
    EXPECT_EQ(queue_.Pop(), withdraw_sh);
    EXPECT_EQ(queue_.Pop(), withdraw_sz);
    EXPECT_EQ(queue_.Pop(), redeem);
    EXPECT_EQ(queue_.Pop(), batch_order);
    EXPECT_EQ(queue_.Pop(), normal_order);
    EXPECT_EQ(queue_.Pop(), nullptr);
}

}  // namespace co
