// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
namespace co {

constexpr int kSubscriberMaxBufferSize = 10000;  // 样本数量 订阅者缓冲区大小
constexpr int kMaxSubscribers = 2;  // 最大订阅者数量
constexpr int kMaxPublishers = 2;  // 最大发布者数量
constexpr int kMemTypeQueryTradeAssetReq = 6400001;
constexpr int kMemTypeQueryTradeAssetRep = 6400002;
constexpr int kMemTypeQueryTradePositionReq = 6400003;
constexpr int kMemTypeQueryTradePositionRep = 6400004;
constexpr int kMemTypeQueryTradeKnockReq = 6400005;
constexpr int kMemTypeQueryTradeKnockRep = 6400006;
constexpr int kMemTypeInnerSignal = 6400007;
constexpr int kMemTypeHeartBeat = 6400008;
constexpr int kMemTypeMonitorRisk = 640000111;

struct MemTradeAccount {
    char fund_id[kMemFundIdSize];
    int64_t timestamp = 0;
    char name[128];
    int64_t type = 0;
};

struct MemQueryMessage {
    char id[kMemIdSize];
    int64_t timestamp;
    char fund_id[kMemFundIdSize];
    char cursor[128];
    char next_cursor[128];
    char error[kMemErrorSize];
};

struct MemOnRspQueryPosition {
    char id[kMemIdSize];  // INIT_ 开头的字段
    bool last_flag;  // 最后一条
    MemTradePosition item;
};

struct MemInnerSignal {
    int64_t timestamp;
    char fund_id[kMemFundIdSize];
};

struct MemHeartBeatMessage {
    int64_t timestamp;
    char fund_id[kMemFundIdSize];
};

struct MemMonitorRiskMessage {
    int64_t timestamp;
    char fund_id[kMemFundIdSize];
    char error[1024];
};

struct QueryContext {
    QueryContext(int32_t _msg_type, int64_t _req_time) : msg_type(_msg_type), req_time(_req_time) {
    }
    int32_t msg_type;
    int64_t req_time = 0;
};

struct MemSingleOrderMessage {
    static constexpr const char* IOX2_TYPE_NAME = "MemSingleOrderMessage";
    char id[kMemIdSize]; // 消息编号
    int64_t timestamp; // 时间戳，示例：20160121091501001
    int64_t trade_type; // 交易类型：1-现货，2-期货，3-期权
    char fund_id[kMemFundIdSize]; // 资金账号
    char username[kMemUsernameSize]; // 用户名
    char token[kMemTokenSize]; // 访问令牌
    char basket_code[kMemBasketCodeSize]; // 篮子代码，示例：篮子001；
    int64_t basket_volume; // （篮子报单/补单必填）篮子个数，大于0表示为篮子委托；
    int64_t basket_price_type; // 篮子价格，示例：7-卖1价下浮1跳
    int64_t basket_size; // （篮子报单必填）篮子大小，单笔委托个数，每个非补单消息必须填写，用于系统识别篮子的完整性；
    int64_t basket_fill; // 是否为篮子补单
    char policy_type[kMemPolicyTypeSize]; // 策略类型，示例：__REPLENISHMENT__
    char policy_id[kMemPolicyIdSize]; // 策略编号，如果是篮子补单则为对应TradeOrderMessage的id
    char tags[kMemTagsSize]; // 标签组（元组），多个关键字逗号分隔
    int64_t bs_flag; // 买卖方向，1-买, 2-卖, 3-申购, 4-赎回
    int64_t timeout; // 自定义超时毫秒数
    char error[kMemErrorSize]; // 错误消息，不为空表示所有委托都报单失败
    char batch_no[kMemBatchNoSize]; // 委托批次号，单笔委托为空
    int64_t rep_time; // 柜台响应时间，示例：20180728231340100
    int64_t update_time; // 最后更新时间，示例：20180728231340100
    int64_t traces[kMemTraceSize]; // 时间戳跟踪，[(node_type, begin_ns, end_ns), ...]
    int64_t items_size;  // 委托个数
    MemTradeOrder item;
};

inline auto operator<<(std::ostream& stream, const MemSingleOrderMessage& msg) -> std::ostream& {
    stream << "\"_type\": \"trade_order_message\", \"id\": " << msg.id << ", \"timestamp\": " << msg.timestamp
    << ", \"fund_id\": \"" << msg.fund_id << "\", \"item: " << ToString(&msg.item);
    return stream;
}

std::string static ToString(const MemSingleOrderMessage* msg) {
    stringstream ss;
    ss << "\"_type\": \"trade_order_message\", \"id\": " << msg->id << ", \"timestamp\": " << msg->timestamp
       << ", \"bs_flag\": " << msg->bs_flag << ", \"timeout\": " << msg->timeout
       << ", \"fund_id\": \"" << msg->fund_id << ", \"error\": \"" << msg->error << "\", \"item: " << ToString(&msg->item);
    return ss.str();
}

struct MemUnionMessage {
    static constexpr const char* IOX2_TYPE_NAME = "MemUnionMessage";
    int32_t msg_type;
    bool free_flag;
    union {
        MemSingleOrderMessage order;
        MemTradeWithdrawMessage withdraw;
        MemTradeAsset asset;
        MemTradePosition pos;
        MemOnRspQueryPosition rsp_pos;
        MemTradeKnock knock;
        MemQueryMessage query;
        MemMonitorRiskMessage monitor;
        MemHeartBeatMessage heart;
        MemInnerSignal inter;
    };
};
}  // namespace co