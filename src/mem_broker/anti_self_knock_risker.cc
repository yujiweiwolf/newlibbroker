// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "anti_self_knock_risker.h"

namespace co {

// ============================================================
// OrderBook 实现
// ============================================================

std::string OrderBook::HandleTradeOrderReq(int64_t bs_flag, int64_t price) {
    if (bs_flag == kBsFlagBuy) {
        for (auto it = asks_.begin(); it != asks_.end(); ) {
            if (it->first > price) break;
            if (it->second->IsFinished()) {
                it = asks_.erase(it);
            } else {
                return BuildSelfKnockError(it->second);
            }
        }
    } else if (bs_flag == kBsFlagSell) {
        for (auto it = bids_.begin(); it != bids_.end(); ) {
            if (it->first < price) break;
            if (it->second->IsFinished()) {
                it = bids_.erase(it);
            } else {
                return BuildSelfKnockError(it->second);
            }
        }
    }
    return "";
}

void OrderBook::OnTradeOrderReqPass(InnerOrderPtr order) {
    if (!order) return;
    if (order->bs_flag != kBsFlagBuy && order->bs_flag != kBsFlagSell) return;

    if (order->bs_flag == kBsFlagBuy) {
        bids_.insert({order->price, order});
    } else {
        asks_.insert({order->price, order});
    }
}

InnerOrderPtr OrderBook::HandleTradeOrderRep(const std::string& message_id, int64_t bs_flag, const MemTradeOrder* order) {
    int64_t price = f2i(order->price);
    bool has_order_no = strnlen(order->order_no, kMemOrderNoSize) > 0;

    if (bs_flag == kBsFlagBuy) {
        auto [begin, end] = bids_.equal_range(price);
        for (auto it = begin; it != end; ++it) {
            if (it->second->message_id == message_id) {
                if (has_order_no) {
                    it->second->order_no = order->order_no;
                    return it->second;
                }
                bids_.erase(it);
                return nullptr;
            }
        }
    } else if (bs_flag == kBsFlagSell) {
        auto [begin, end] = asks_.equal_range(price);
        for (auto it = begin; it != end; ++it) {
            if (it->second->message_id == message_id) {
                if (has_order_no) {
                    it->second->order_no = order->order_no;
                    return it->second;
                }
                asks_.erase(it);
                return nullptr;
            }
        }
    }
    return nullptr;
}

std::string OrderBook::BuildSelfKnockError(const InnerOrderPtr& other) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "[FAN-RISK-ERROR][防对敲]风控检查失败，存在%s挂单：[%s][%s][%s] price: %ld, volume: %ld, order_no: %s",
        other->bs_flag == kBsFlagBuy ? "买" : "卖",
        x::RawTimeText(other->timestamp % 1000000000LL).data(),
        other->fund_id.c_str(),
        other->code.c_str(),
        other->price,
        other->volume,
        other->order_no.c_str());
    return std::string(buf);
}

// ============================================================
// AntiSelfKnockRisk 实现
// ============================================================

std::shared_ptr<OrderBook> AntiSelfKnockRisk::MustGetOrderBook(const std::string& code) {
    auto it = order_books_.find(code);
    if (it != order_books_.end()) {
        return it->second;
    }
    auto book = std::make_shared<OrderBook>();
    order_books_[code] = book;
    return book;
}

std::string AntiSelfKnockRisk::HandleTradeOrderReq(MemTradeOrderMessage* req) {
    if (!req) return "empty request";

    for (int64_t i = 0; i < req->items_size; ++i) {
        auto& item = req->items[i];
        if (item.bs_flag != kBsFlagBuy && item.bs_flag != kBsFlagSell) continue;

        auto book = MustGetOrderBook(std::string(item.code, strnlen(item.code, kMemCodeSize)));
        std::string err = book->HandleTradeOrderReq(item.bs_flag, f2i(item.price));
        if (!err.empty()) {
            LOG_WARN << err;
            return err;
        }
    }
    return "";
}

void AntiSelfKnockRisk::OnTradeOrderReqPass(MemTradeOrderMessage* req) {
    if (!req) return;

    for (int64_t i = 0; i < req->items_size; ++i) {
        auto& item = req->items[i];
        if (item.bs_flag != kBsFlagBuy && item.bs_flag != kBsFlagSell) continue;

        auto inner = std::make_shared<InnerOrder>();
        inner->message_id = req->id;
        inner->fund_id = std::string(req->fund_id, strnlen(req->fund_id, kMemFundIdSize));
        inner->code = std::string(item.code, strnlen(item.code, kMemCodeSize));
        inner->bs_flag = item.bs_flag;
        inner->price = f2i(item.price);
        inner->volume = item.volume;
        inner->timestamp = item.timestamp;
        inner->create_time = x::UnixMilli();

        auto book = MustGetOrderBook(inner->code);
        book->OnTradeOrderReqPass(inner);

        LOG_INFO << "insert book, code: " << inner->code
                 << ", message_id: " << inner->message_id
                 << ", bs_flag: " << item.bs_flag
                 << ", price: " << item.price
                 << ", volume: " << item.volume;
    }
}

void AntiSelfKnockRisk::HandleTradeOrderRep(MemTradeOrderMessage* rep) {
    if (!rep) return;

    std::unique_ptr<std::vector<MemTradeKnock>> knocks;
    std::string batch_no  = rep->batch_no;
    for (int64_t i = 0; i < rep->items_size; ++i) {
        auto& item = rep->items[i];
        std::string order_no = string(item.order_no);

        auto book = MustGetOrderBook(item.code);
        InnerOrderPtr inner = book->HandleTradeOrderRep(rep->id, item.bs_flag, &item);
        if (!inner) continue;

        if (!order_no.empty()) {
            // 先收到成交回报, 后收到报单响应
            if (auto it = knock_first_orders_.find(order_no); it != knock_first_orders_.end()) {
                if (!knocks) {
                    knocks = std::make_unique<std::vector<MemTradeKnock>>();
                }
                for (auto& k : *it->second) {
                    knocks->push_back(std::move(k));
                }
                knock_first_orders_.erase(it);
            }
            single_orders_[order_no] = inner;
            if (!batch_no.empty()) {
                auto bit = batch_orders_.find(batch_no);
                if (bit == batch_orders_.end()) {
                    bit = batch_orders_.emplace(batch_no, std::make_unique<std::vector<InnerOrderPtr>>()).first;
                }
                bit->second->push_back(inner);
//                LOG_INFO << "HandleTradeOrderRep, batch_no： " << batch_no
//                         << ", order_no： " << order_no
//                         << ", code： " << item.code
//                         << ", bs_flag： " << item.bs_flag
//                         << ", batch_orders_ size： " << bit->second->size();
            }
        }
    }
    if (knocks) {
        for (auto& it : *knocks) {
            OnTradeKnock(&it);
        }
    }
//    LOG_INFO << "HandleTradeOrderRep end, batch_orders_ count： " << batch_orders_.size();
//    for (auto& [bn, orders] : batch_orders_) {
//        LOG_INFO << "HandleTradeOrderRep end, batch_no： " << bn << ", alive orders： " << orders->size();
//    }
}

std::string AntiSelfKnockRisk::HandleTradeWithdrawReq(MemTradeWithdrawMessage* req) {
    if (strlen(req->order_no) > 0) {
        if (auto it = single_orders_.find(req->order_no); it != single_orders_.end()) {
            it->second->withdraw_time = x::UnixMilli();
        }
    } else if (strlen(req->batch_no) > 0){
        if (auto it = batch_orders_.find(req->batch_no); it != batch_orders_.end()) {
            for (auto& order : *it->second) {
                order->withdraw_time = x::UnixMilli();
            }
        }
    }
    return "";
}

void AntiSelfKnockRisk::HandleTradeWithdrawRep(MemTradeWithdrawMessage* rep) {
    if (strlen(rep->order_no) > 0) {
        if (auto it = single_orders_.find(rep->order_no); it != single_orders_.end()) {
            if (strlen(rep->error) == 0) {
                it->second->withdraw_succeed = true;
            }

        }
    } else if (strlen(rep->batch_no) > 0){
        if (auto it = batch_orders_.find(rep->batch_no); it != batch_orders_.end()) {
            for (auto& order : *it->second) {
                order->withdraw_succeed = true;
            }
        }
    }
}

void AntiSelfKnockRisk::OnTradeKnock(MemTradeKnock* knock) {
    std::string order_no = knock->order_no;
    auto it = single_orders_.find(order_no);
    if (it == single_orders_.end()) {
        LOG_INFO << "knock first, order_no second, fund_id: " << knock->fund_id
                         << ", code: " << knock->code
                         << ", order_no: " <<  knock->order_no
                         << ", batch_no: " <<  knock->batch_no
                         << ", match_no: " << knock->match_no
                         << ", match_type: " << knock->match_type
                         << ", match_volume: " << knock->match_volume;
        auto kit = knock_first_orders_.find(order_no);
        if (kit == knock_first_orders_.end()) {
            auto knocks = std::make_unique<std::vector<MemTradeKnock>>();
            knocks->push_back(*knock);
            kit = knock_first_orders_.emplace(order_no, std::move(knocks)).first;
        } else {
            kit->second->push_back(*knock);
        }
    } else {
        auto& order = it->second;
        if (knock->match_type == kMatchTypeOK) {
                order->match_volume += knock->match_volume;
        } else if (knock->match_type == kMatchTypeWithdrawOK || knock->match_type == kMatchTypeFailed) {
            order->withdraw_succeed = true;
        }
        if (order->IsFinished()) {
            single_orders_.erase(it);
            if (auto item = batch_orders_.find(knock->batch_no); item != batch_orders_.end()) {
                auto& orders = item->second;
                bool all_finished = true;
                for (auto& active_order: *orders) {
                    if (!active_order->IsFinished()) {
                        all_finished = false;
                        break;
                    }
                }
                if (all_finished) {
                    batch_orders_.erase(item);
                }
            }
        }
    }    
}

}  // namespace co
