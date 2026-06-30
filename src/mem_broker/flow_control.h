// Copyright 2026 Fancapital Inc.  All rights reserved.
#pragma once
#include <queue>
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "utils.h"

namespace co {
constexpr double kFlowControlPriorityWithdraw = 300000000000;
constexpr double kFlowControlPriorityCreateRedeem = 200000000000;
constexpr int64_t kFlowControlWindowMS = 1500;  // 流控时间窗口，1秒+500ms安全垫；

struct MemFlowControlStateItem {
    int64_t market;
    int64_t total_cmd_size;
    int64_t timestamp;
};

struct MemFlowControlState {
    char fund_id[co::kMemFundIdSize];
    MemFlowControlStateItem items[100];
};

struct FlowControlItem {
    FlowControlItem(int64_t _timestamp, double _order_amount, int64_t _sub_order_size, char* _msg) : timestamp(_timestamp), order_amount(_order_amount), sub_order_size(_sub_order_size), msg(_msg) {
    }
    int64_t timestamp = 0;  // 消息时间
    double order_amount = 0;  // 其他类型的委托金额
    int64_t sub_order_size = 0;  // 子指令数量
    char* msg = nullptr;  // MemFrameHeader + body
};

struct CompareFlowControlItemPtr {
    bool operator()(const std::shared_ptr<FlowControlItem>& a,
                    const std::shared_ptr<FlowControlItem>& b) const {
        // priority_queue 默认是大顶堆（less），返回 true 表示 a 优先级低于 b
        // order_amount 大的优先级高
        if (a->order_amount != b->order_amount)
            return a->order_amount < b->order_amount;
        // order_amount 相同时，timestamp 小的优先级高
        return a->timestamp > b->timestamp;
    }
};

/**
 * 根据市场进行分组的流控队列
 */
class FlowControlMarketQueue {
public:
    FlowControlMarketQueue(MemFlowControlStateItem* state, FlowControlConfig* cfg);
    void Push(const std::shared_ptr<FlowControlItem>& item);
    std::shared_ptr<FlowControlItem> TryPop(int64_t now_dt);
    std::shared_ptr<FlowControlItem> GetMaxPriorityItem(int64_t now_dt);

private:
    int64_t market_ = 0; // 市场代码
    int64_t th_tps_limit_ = 0; // 每秒报撤单流控阈值
    int64_t th_tps_queue_text_warning_ = 0; // 每秒报撤单流控-排队长度文字报警阈值
    int64_t th_tps_queue_voice_warning_ = 0; // 每秒报撤单流控-排队长度语音报警阈值
    int64_t th_daily_warning_ = 0; // 全天报撤单预警阈值
    int64_t th_daily_limit_ = 0; // 全天报撤单流控阈值
    MemFlowControlStateItem* state_;

    std::deque<int64_t> sent_ns_queue_;  // 系统发送时间队列
    std::priority_queue<std::shared_ptr<FlowControlItem>, std::vector<std::shared_ptr<FlowControlItem>>, CompareFlowControlItemPtr> flow_control_queue_;  // 需要进行流控的消息队列（堆排序）
    int64_t cmd_size_ = 0;  // 当前流控队列中的子指令数量之和；
    int64_t total_cmd_size_ = 0;  // 已发送的所有指令个数
    std::atomic_int64_t triggered_flow_control_size_ = 0;  // 已触发流控的最新流控队列大小，用于报警；
    std::atomic_int64_t pre_warning_total_cmd_size_ = 0; // 上次报警的总指令个数
};


class FlowControlQueue {
 public:
    void Init(MemBrokerOptionsPtr option);
    void Push(char* msg);
    char* Pop();

 private:
    std::shared_ptr<FlowControlMarketQueue> GetMarketQueue(int64_t market);
    int64_t idle_sleep_ns_ = 0;
    // kMarketSH=1 kMarketSZ=2 kMarketBJ=3，数组下标对应 market 值
    static constexpr int kMarketQueueSize = 4;
    std::shared_ptr<FlowControlMarketQueue> market_to_queue_[kMarketQueueSize];
    std::deque<char*> normal_queue_;  // 不需要进行流控的其他消息队列
};
}
