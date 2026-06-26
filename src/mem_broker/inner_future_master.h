// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "mem_struct.h"
#include "options.h"

namespace co {
struct InnerFuturePosition {
    InnerFuturePosition(std::string code, int64_t bs_flag) : code_(code) {
        if (bs_flag == kBsFlagBuy) {
            tag_ = "多头持仓";
        } else {
            tag_ = "空头持仓";
        }
        marker_ = co::CodeToMarket(code);
    }

    int64_t GetYesterdayAvailableVolume() {
        return (yd_init_volume_ - yd_closing_volume_ - yd_close_volume_);
    }

    int64_t GetTodayAvailableVolume() {
        return (td_init_volume_ +  td_open_volume_ - td_closing_volume_ - td_close_volume_);
    }

    std::string ToString() {
        std::stringstream ss;
        ss << "InnerPosition{";
        ss << "code: " << code_ << ", " << tag_
           << ", yd_init_volume: " << yd_init_volume_
           << ", yd_closing_volume: " << yd_closing_volume_
           << ", yd_close_volume: " << yd_close_volume_
           << ", td_init_volume: " << td_init_volume_
           << ", td_closing_volume: " << td_closing_volume_
           << ", td_close_volume: " << td_close_volume_
           << ", td_open_volume: " << td_open_volume_
           << "}";
        return ss.str();
    }

    std::string code_;
    int64_t marker_;
    std::string tag_;               // 多头持仓, 空头持仓
    int64_t yd_init_volume_ = 0;     // broker启动时的昨日持仓, 有平仓交易, 重启后会变小
    int64_t yd_closing_volume_ = 0;  // 昨日持仓平仓冻结数
    int64_t yd_close_volume_ = 0;    // 昨日持仓已平仓数

    int64_t td_init_volume_ = 0;     // broker启动时的今日持仓
    int64_t td_closing_volume_ = 0;  // 今日持仓平仓冻结数
    int64_t td_close_volume_ = 0;    // 今日持仓已平仓数
    int64_t td_opening_volume_ = 0;  // 今日持仓开仓冻结数, 只显示，没有作用
    int64_t td_open_volume_ = 0;     // 今日已开仓数
};
typedef std::shared_ptr<InnerFuturePosition> InnerFuturePositionPtr;

class InnerFutureMaster {
 public:
    explicit InnerFutureMaster(int64_t trade_type);

    void InitPositions(MemTradePosition* position, bool last);
    void HandleOrderReq(MemTradeOrderMessage* req);
    void HandleOrderRep(MemTradeOrderMessage* rep);
    void HandleKnock(const MemTradeKnock& knock);

    int64_t GetAutoOcFlag(int64_t bs_flag, const MemTradeOrder& order);
    int64_t GetCloseYesterdayFlag(int64_t bs_flag, const MemTradeOrder& order);
    InnerFuturePositionPtr GetPosition(std::string code, int64_t bs_flag, int64_t oc_flag);

 protected:
    void InitCffexParam();
    void Update(InnerFuturePositionPtr pos, int64_t oc_flag, int64_t order_volume, int64_t match_volume, int64_t withdraw_volume);

 private:
    int64_t trade_type_ = 0;  // 1 现货, 2 期货, 3 期权, 4 信用
    std::map<std::string, std::shared_ptr<std::pair<InnerFuturePositionPtr, InnerFuturePositionPtr>>> positions_;  // <code> -> first is buy, second is sell
    std::set<std::string> knocks_;  // <inner_match_no>

    bool forbid_closing_today_ = false;  // 风控策略：禁止股指期货自动开平仓时平今仓
    int64_t max_today_opening_volume_ = 0;  // 风控策略：限制股指期货当日最大开仓数
    std::map<string, int64_t> open_cache_;  // 合约类型（IF、IH、IC） -> 已开仓数 + 开仓冻结数，用于限制当日最大开仓数
};
typedef std::shared_ptr<InnerFutureMaster> InnerFutureMasterPtr;
}  // namespace co
