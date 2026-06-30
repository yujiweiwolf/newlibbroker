// Copyright 2026 Fancapital Inc.  All rights reserved.
#include <filesystem>
#include "flow_control.h"
#include "mem_struct.h"

namespace co {
FlowControlMarketQueue::FlowControlMarketQueue(MemFlowControlStateItem* state, FlowControlConfig* cfg) : state_(state) {
    total_cmd_size_ = state_->total_cmd_size;
    market_ = cfg->market();
    th_tps_limit_ = cfg->th_tps_limit();
    th_tps_queue_text_warning_ = cfg->th_tps_queue_text_warning();
    th_tps_queue_voice_warning_ = cfg->th_tps_queue_voice_warning();
    th_daily_warning_ = cfg->th_daily_warning();
    th_daily_limit_ = cfg->th_daily_limit();
    LOG_INFO << "market: " << market_ << ", total_cmd_size: " << total_cmd_size_
             << ", th_tps_limit: " << th_tps_limit_ << ", th_daily_limit: " << th_daily_limit_;
}

void FlowControlMarketQueue::Push(const std::shared_ptr<FlowControlItem>& item) {
    flow_control_queue_.push(item);
    cmd_size_++;
}

std::shared_ptr<FlowControlItem> FlowControlMarketQueue::TryPop(int64_t now_dt) {
    if (flow_control_queue_.empty()) {
        return nullptr;
    }
    auto item = flow_control_queue_.top();
    flow_control_queue_.pop();
    cmd_size_ -= item->sub_order_size;
    for (int64_t i = 0; i < item->sub_order_size; ++i) {
        sent_ns_queue_.emplace_back(now_dt);
    }
    total_cmd_size_ += item->sub_order_size;   
    if (state_) {
        state_->total_cmd_size += item->sub_order_size; 
    }
    return item;
}

std::shared_ptr<FlowControlItem> FlowControlMarketQueue::GetMaxPriorityItem(int64_t now_dt) {
    while (!sent_ns_queue_.empty()) {
        auto &sent_dt = sent_ns_queue_.front();
        if (x::SubRawDateTime(now_dt, sent_dt) > kFlowControlWindowMS) {
            sent_ns_queue_.pop_front();
        } else {
            break;
        }
    }
    if (flow_control_queue_.empty()) {
        return nullptr;
    }
    auto top = flow_control_queue_.top();
    if (static_cast<int64_t>(sent_ns_queue_.size()) + top->sub_order_size <= th_tps_limit_) {
        if ((state_->total_cmd_size + top->sub_order_size) <= th_daily_limit_) {
            return top;
        } else {
            // 超过全天阈值，给 msg 赋值错误信息
            auto* frame = reinterpret_cast<MemFrameHeader*>(top->msg);
            char* body = top->msg + sizeof(MemFrameHeader);
            if (frame->type == kMemTypeTradeOrderReq) {
                auto* order_msg = reinterpret_cast<MemTradeOrderMessage*>(body);
                sprintf(order_msg->error, "[FAN-BROKER-ERROR] 超过全天报撤单流控阈值: %ld, 已用: %ld", th_daily_limit_, state_->total_cmd_size);
            } else if (frame->type == kMemTypeTradeWithdrawReq) {
                auto* withdraw_msg = reinterpret_cast<MemTradeWithdrawMessage*>(body);
                sprintf(withdraw_msg->error, "[FAN-BROKER-ERROR] 超过全天报撤单流控阈值: %ld, 已用: %ld", th_daily_limit_, state_->total_cmd_size);
            }
            return top;
        }
    } else {
        return nullptr;
    }
}

// ============================================================================================
void FlowControlQueue::Init(MemBrokerOptionsPtr option) {
    string dir = "../data";
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
    std::string state_path = dir + "/state." + std::to_string(x::RawDate()) + ".mem";
    MemFlowControlState* state = (MemFlowControlState*)map_file(state_path.c_str(), 64 << 20);
    idle_sleep_ns_ = option->idle_sleep_ns();
    for (auto& cfg: option->flow_controls()) {
        auto market = cfg->market();
        auto queue = std::make_shared<FlowControlMarketQueue>(&state->items[market], &*cfg);
        if (market >= 0 && market < kMarketQueueSize) {
            market_to_queue_[market] = queue;
        }
    }
}

std::shared_ptr<FlowControlMarketQueue> FlowControlQueue::GetMarketQueue(int64_t market) {
    if (market >= 0 && market < kMarketQueueSize) {
        return market_to_queue_[market];
    }
    return nullptr;
}

void FlowControlQueue::Push(char* msg) {
    auto* frame = reinterpret_cast<MemFrameHeader*>(msg);

    if (frame->type == kMemTypeTradeOrderReq) {
        // 报单：按市场分流控队列
        auto* order_msg = reinterpret_cast<MemTradeOrderMessage*>(msg + sizeof(MemFrameHeader));
        if (order_msg->items_size > 0) {
            int64_t market = order_msg->items[0].market;
            int64_t items_size = order_msg->items_size;
            double order_amount = 0;
            if (order_msg->bs_flag == kBsFlagBuy || order_msg->bs_flag == kBsFlagSell) {
                // 按委托金额 = price * volume
                for (int i = 0; i < items_size; ++i) {
                    order_amount += order_msg->items[i].price * order_msg->items[i].volume;
                }
            } else {
                // 申购/赎回：中间优先级
                order_amount = kFlowControlPriorityCreateRedeem;
            }
            auto queue = GetMarketQueue(market);
            if (queue) {
                auto item = std::make_shared<FlowControlItem>(x::RawDateTime(), order_amount, items_size, msg);
                queue->Push(item);
                return;
            } else {
                normal_queue_.push_back(msg);  // 无需流控或找不到对应市场队列时，入普通队列
            }
        }
    } else if (frame->type == kMemTypeTradeWithdrawReq) {
        // 撤单：高优先级
        auto* withdraw_msg = reinterpret_cast<MemTradeWithdrawMessage*>(msg + sizeof(MemFrameHeader));
        int64_t market = withdraw_msg->market;
        int64_t withdraw_size = 1;
        if (strlen(withdraw_msg->batch_no) >= 3) {
            // batch_no 格式: 1_400_4324242 (market_subOrderSize_seq)
            std::string batch_no(withdraw_msg->batch_no);
            auto first = batch_no.find('_');
            if (first != std::string::npos) {
                auto second = batch_no.find('_', first + 1);
                if (second != std::string::npos) {
                    withdraw_size = std::atoll(batch_no.substr(first + 1, second - first - 1).c_str());
                }
            }
        }
        auto queue = GetMarketQueue(market);
        if (queue) {
            auto item = std::make_shared<FlowControlItem>(x::RawDateTime(), kFlowControlPriorityWithdraw, withdraw_size, msg);
            queue->Push(item);
            return;
        } else {
            normal_queue_.push_back(msg);   // 无需流控或找不到对应市场队列时，入普通队列
        }
    } else {
        normal_queue_.push_back(msg);   // 无需流控或找不到对应市场队列时，入普通队列
    }
}

char* FlowControlQueue::Pop() {
    // 从三个交易所队列中找优先级最高的
    int best_market = -1;
    std::shared_ptr<FlowControlItem> best_item;
    int64_t now = x::RawDateTime();

    for (int i = 1; i < kMarketQueueSize; ++i) {
        auto& queue = market_to_queue_[i];
        if (!queue) continue;
        auto item = queue->GetMaxPriorityItem(now);
        if (!item) continue;

        if (!best_item ||
            item->order_amount > best_item->order_amount ||
            (item->order_amount == best_item->order_amount && item->timestamp < best_item->timestamp)) {
            best_item = item;
            best_market = i;
        }
    }

    if (best_market > 0) {
        auto item = market_to_queue_[best_market]->TryPop(now);
        if (item) {
            return item->msg;
        }
    }

    // 最后处理普通队列
    if (!normal_queue_.empty()) {
        auto* msg = normal_queue_.front();
        normal_queue_.pop_front();
        return msg;
    }
    return nullptr;
}
}
