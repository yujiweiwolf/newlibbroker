// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include "x/x.h"

using std::string;
namespace co {
class FlowControlConfig {
 public:
    std::string ToString() const;
    [[nodiscard]] inline int64_t market() const {
        return market_;
    }

    inline void set_market(int64_t market) {
        market_ = market;
    }

    [[nodiscard]] inline int64_t th_tps_limit() const {
        return th_tps_limit_;
    }

    inline void set_th_tps_limit(int64_t th_tps_limit) {
        th_tps_limit_ = th_tps_limit;
    }

    [[nodiscard]] inline int64_t th_tps_queue_text_warning() const {
        return th_tps_queue_text_warning_;
    }

    inline void set_th_tps_queue_text_warning(int64_t th_tps_queue_text_warning) {
        th_tps_queue_text_warning_ = th_tps_queue_text_warning;
    }

    [[nodiscard]] inline int64_t th_tps_queue_voice_warning() const {
        return th_tps_queue_voice_warning_;
    }

    inline void set_th_tps_queue_voice_warning(int64_t th_tps_queue_voice_warning) {
        th_tps_queue_voice_warning_ = th_tps_queue_voice_warning;
    }

    [[nodiscard]] inline int64_t th_daily_warning() const {
        return th_daily_warning_;
    }

    inline void set_th_daily_warning(int64_t th_daily_warning) {
        th_daily_warning_ = th_daily_warning;
    }

    [[nodiscard]] inline int64_t th_daily_limit() const {
        return th_daily_limit_;
    }

    inline void set_th_daily_limit(int64_t th_daily_limit) {
        th_daily_limit_ = th_daily_limit;
    }

private:
    int64_t market_ = 0; // 市场代码
    int64_t th_tps_limit_ = 0; // 每秒报撤单流控阈值
    int64_t th_tps_queue_text_warning_ = 0; // 每秒报撤单流控-排队长度文字报警阈值
    int64_t th_tps_queue_voice_warning_ = 0; // 每秒报撤单流控-排队长度语音报警阈值
    int64_t th_daily_warning_ = 0; // 全天报撤单预警阈值
    int64_t th_daily_limit_ = 0; // 全天报撤单流控阈值
};

class MemBrokerOptions {
 public:
    static std::shared_ptr<MemBrokerOptions> Load(const std::string& filename = "");

    std::string ToString();

    bool IsFlowControlEnabled() const;

    inline bool disable_flow_control() const {
        return disable_flow_control_;
    }

    inline const std::vector<std::unique_ptr<FlowControlConfig>>& flow_controls() const {
        return flow_controls_;
    }

    inline std::vector<std::unique_ptr<FlowControlConfig>>* mutable_flow_controls() {
        return &flow_controls_;
    }

    inline void set_request_timeout_ms(int64_t request_timeout_ms) {
        request_timeout_ms_ = request_timeout_ms;
    }

    inline int64_t request_timeout_ms() const {
        return request_timeout_ms_;
    }

    inline int64_t query_asset_interval_ms() const {
        return query_asset_interval_ms_;
    }

    inline int64_t query_position_interval_ms() const {
        return query_position_interval_ms_;
    }

    inline int64_t query_knock_interval_ms() const {
        return query_knock_interval_ms_;
    }

    inline int64_t idle_sleep_ns() const {
        return idle_sleep_ns_;
    }

    inline int64_t cpu_affinity() const {
        return cpu_affinity_;
    }

    inline int64_t batch_order_size() const {
        return batch_order_size_;
    }

    inline int trade_type() const {
        return trade_type_;
    }

    inline bool enable_query_only() const {
        return enable_query_only_;
    }

    inline bool enable_upload() const {
        return enable_upload_;
    }
    inline std::string node_name() const {
        return node_name_;
    }
    inline std::string mem_dir() const {
        return mem_dir_;
    }
    inline std::string mem_req_file() const {
        return mem_req_file_;
    }
    inline std::string mem_rep_file() const {
        return mem_rep_file_;
    }
    inline int64_t req_stream_id() const {
        return req_stream_id_;
    }
    inline int64_t rep_stream_id() const {
        return rep_stream_id_;
    }
    inline int64_t inner_stream_id() const {
        return inner_stream_id_;
    }
    inline void set_inner_stream_id(int64_t stream_id) {
        inner_stream_id_ = stream_id;
    }
    inline std::string aeron_channel() const {
        return aeron_channel_;
    }

 private:
    std::shared_ptr<x::LoggingOptions> log_opt_;
    std::string trade_gateway_;
    std::string wal_;
    std::string node_name_;

    bool enable_upload_ = true;  // 是否启用上传交易数据

    int64_t request_timeout_ms_ = 5000;  // 请求超时时间
    bool disable_flow_control_ = false;  // 强制明确禁用流控
    std::vector<std::unique_ptr<FlowControlConfig>> flow_controls_;
    int64_t batch_order_size_ = 1;  // 批量委托的篮子上限

    int trade_type_ = 0;
    bool enable_query_only_ = false;  // 是否启用只查询模式，不接收报单和撤单等指令

    int64_t query_asset_interval_ms_ = 0;  // 资金查询时间间隔
    int64_t query_position_interval_ms_ = 0;  // 持仓查询时间间隔
    int64_t query_knock_interval_ms_ = 0;  // 成交查询时间间隔
    int64_t idle_sleep_ns_ = 100000;  // 无锁队列空转时休眠时间（单位：纳秒）
    int64_t cpu_affinity_ = -1;  // CPU核绑定
    string mem_dir_;
    string mem_req_file_;
    string mem_rep_file_;
    int64_t req_stream_id_ = 0;  // iceoryx2 请求流 ID
    int64_t rep_stream_id_ = 0;  // iceoryx2 应答流 ID
    int64_t inner_stream_id_ = 0;  // 内部流 ID
    string aeron_channel_;       // aeron channel 配置
};
    typedef std::shared_ptr<MemBrokerOptions> MemBrokerOptionsPtr;
}  // namespace co

