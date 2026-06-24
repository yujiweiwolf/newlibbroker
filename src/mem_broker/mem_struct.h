// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
namespace co {

constexpr int kMemTypeQueryTradeAssetReq = 6400001;
constexpr int kMemTypeQueryTradeAssetRep = 6400002;
constexpr int kMemTypeQueryTradePositionReq = 6400003;
constexpr int kMemTypeQueryTradePositionRep = 6400004;
constexpr int kMemTypeQueryTradeKnockReq = 6400005;
constexpr int kMemTypeQueryTradeKnockRep = 6400006;
constexpr int kMemTypeInnerSignal = 6400007;
constexpr int kMemTypeHeartBeat = 6400008;
constexpr int kMemTypeMonitorRisk = 640000111;

struct MemFrameHeader {
    int64_t type = 0;
    int64_t body_length = 0;
};

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
}  // namespace co