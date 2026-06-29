// Copyright 2025 Fancapital Inc.  All rights reserved.

#include "yaml-cpp/yaml.h"
#include "position_master.h"
constexpr int kCFFEXOptionLength = 14;

namespace co {
PositionMaster::PositionMaster(int64_t trade_type) : trade_type_(trade_type) {
}

void PositionMaster::InitCffexParam() {
    auto getBool = [&](const YAML::Node& node, const std::string& name) {
        try {
            return node[name] && !node[name].IsNull() ? node[name].as<bool>() : false;
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name = " << name << ", error = " << e.what();
            throw std::runtime_error(e.what());
        }
    };
    auto getInt = [&](const YAML::Node& node, const std::string& name, const int64_t& default_value = 0) {
        try {
            return node[name] && !node[name].IsNull() ? node[name].as<int64_t>() : default_value;
        } catch (std::exception& e) {
            LOG_ERROR << "load configuration failed: name = " << name << ", error = " << e.what();
            throw std::runtime_error(e.what());
        }
    };
    try {
        auto filename = x::FindFile("broker.yaml");
        YAML::Node root = YAML::LoadFile(filename);
        auto param = root["cffex"];
        forbid_closing_today_ = getBool(param, "forbid_closing_today");
        max_today_opening_volume_ = getInt(param, "max_today_opening_volume");
        LOG_INFO << "股指特定参数 forbid_closing_today: " << forbid_closing_today_ << ", max_today_opening_volume: " << max_today_opening_volume_;
    } catch (std::exception& e) {
        LOG_ERROR << "InitCffexParam, " << e.what();
    }
}

void PositionMaster::InitPositions(MemTradePosition* position, bool last) {
    if (trade_type_ == kTradeTypeFuture) {
        LOG_INFO << "init future position";
        string code = position->code;
        InnerPositionPtr long_pos = GetPosition(code, kBsFlagBuy, kOcFlagOpen);
        InnerPositionPtr short_pos = GetPosition(code, kBsFlagSell, kOcFlagOpen);
        long_pos->yd_init_volume_ = position->long_pre_volume;
        long_pos->td_init_volume_ = position->long_volume - position->long_pre_volume;
        short_pos->yd_init_volume_ = position->short_pre_volume;
        short_pos->td_init_volume_ = position->short_volume - position->short_pre_volume;
        if (long_pos->marker_ == kMarketCFFEX && long_pos->td_init_volume_) {
            string type = code.length() > 2 ? code.substr(0, 2) : "";
            if (auto it = open_cache_.find(type); it != open_cache_.end()) {
                it->second += long_pos->td_init_volume_;
            }
        }
        if (short_pos->marker_ == kMarketCFFEX && short_pos->td_init_volume_) {
            string type = code.length() > 2 ? code.substr(0, 2) : "";
            if (auto it = open_cache_.find(type); it != open_cache_.end()) {
                it->second += short_pos->td_init_volume_;
            }
        }
    } if (trade_type_ == kTradeTypeOption) {
        LOG_INFO << "init option position";
        InnerPositionPtr long_pos = GetPosition(position->code, kBsFlagBuy, kOcFlagOpen);
        InnerPositionPtr short_pos = GetPosition(position->code, kBsFlagSell, kOcFlagOpen);
        // 期权无昨仓的概念，全是今仓
        long_pos->td_init_volume_ = position->long_can_close;
        short_pos->td_init_volume_ = position->short_can_close;
    } if (trade_type_ == kTradeTypeCredit) {
        LOG_INFO << "init credit position";
        InnerPositionPtr long_pos = GetPosition(position->code, kBsFlagSell, kOcFlagAuto);
        // 信用交易，担保品可卖的全是昨仓，不考虑T0
        long_pos->yd_init_volume_ = position->long_can_close;
    }
    if (last) {
        LOG_INFO << "[AutoOpenClose] OnInit";
        for (auto it : positions_) {
            LOG_INFO << it.first << ", long:  " << it.second->first->ToString();
            LOG_INFO << it.first << ", short: " << it.second->second->ToString();
        }
        if (trade_type_ == kTradeTypeFuture) {
            InitCffexParam();
//            open_cache_.insert(std::make_pair("IF", 0));
//            open_cache_.insert(std::make_pair("IH", 0));
//            open_cache_.insert(std::make_pair("IC", 0));
//            open_cache_.insert(std::make_pair("IM", 0));
        }
    }
}

void PositionMaster::HandleOrderReq(MemTradeOrderMessage* req) {
    int64_t bs_flag = req->bs_flag;
    const MemTradeOrder& order = req->items[0];
    std::string code = order.code;
    int64_t oc_flag = order.oc_flag;
    InnerPositionPtr pos = GetPosition(code, bs_flag, oc_flag);
    if (pos) {
        std::string before = pos->ToString();
        Update(pos, oc_flag, order.volume, 0, 0);
        std::string after = pos->ToString();
        LOG_INFO << "[AutoOpenClose]["
                 << code << ", bs_flag: " << bs_flag << "] OnOrderReq: "
                 << "oc_flag: " << oc_flag
                 << ", order_volume: " << order.volume
                 << ", before " << before << ", after " << after;
    }
}

void PositionMaster::HandleOrderRep(MemTradeOrderMessage* req) {
    int64_t bs_flag = req->bs_flag;
    const MemTradeOrder& order = req->items[0];
    // 处理委托废单响应，解冻数量
    if (strlen(order.order_no) > 0) {
        LOG_INFO << "报单成功, 不处理, code: " << order.code
                        << ", bs_flag: " << bs_flag
                        << ", oc_flag: " << order.oc_flag
                        << ", order_no: " << order.order_no
                        << ", price: " << order.price
                        << ", volume: " << order.volume;
        return;
    }
    std::string code = order.code;
    int64_t oc_flag = order.oc_flag;
    InnerPositionPtr pos = GetPosition(code, bs_flag, oc_flag);
    if (pos) {
        std::string before = pos->ToString();
        Update(pos, oc_flag, 0, 0, order.volume);
        std::string after = pos->ToString();
        LOG_INFO << "[AutoOpenClose]["
            << code << ", bs_flag: " << bs_flag << "] OnOrderRep: "
            << "oc_flag: " << oc_flag
            << ", withdraw_volume: " << order.volume
            << ", before " << before << ", after " << after;
    }
}

void PositionMaster::HandleKnock(const MemTradeKnock& knock) {
    int64_t bs_flag = knock.bs_flag;
    int64_t oc_flag = knock.oc_flag;
    if (bs_flag != kBsFlagBuy && bs_flag != kBsFlagSell) {
        return;
    }
    if (knock.match_volume <= 0) {
        return;
    }
    int64_t match_volume = 0;
    int64_t withdraw_volume = 0;
    if (knock.match_type == kMatchTypeOK) {
        match_volume = knock.match_volume;
    } else if (knock.match_type == kMatchTypeWithdrawOK || knock.match_type == kMatchTypeFailed) {
        withdraw_volume = knock.match_volume;
    }
    InnerPositionPtr pos = GetPosition(knock.code, bs_flag, oc_flag);
    if (pos && (match_volume > 0 || withdraw_volume > 0)) {
        std::string before = pos->ToString();
        Update(pos, oc_flag, 0, knock.match_volume, withdraw_volume);
        std::string after = pos->ToString();
        LOG_INFO << "[AutoOpenClose]["
            << knock.code << ", bs_flag: " << bs_flag << "] OnKnock: "
            << "oc_flag: " << oc_flag
            << ", order_no: " << knock.order_no
            << ", match_no: " << knock.match_no
            << ", match_type: " << knock.match_type
            << ", match_volume: " << knock.match_volume
            << ", before " << before << ", after " << after;
    }
}

void PositionMaster::Update(InnerPositionPtr pos, int64_t oc_flag, int64_t order_volume, int64_t match_volume, int64_t withdraw_volume) {
    if (!pos) {
        return;
    }
    if (trade_type_ == kTradeTypeFuture) {
        // 内部持仓更新逻辑
        // 1.买开（更新买持仓）
        // 1.1 买开委托：增加开仓冻结；
        // 1.2 买开成交：减少开仓冻结，增加持仓，增加已开仓数；
        // 1.3 买开撤单：减少开仓冻结；
        // 2.买平（更新卖持仓）
        // 2.1 买平委托：减少持仓，增加平仓冻结；
        // 2.2 买平成交：减少平仓冻结，增加已平仓数；
        // 2.3 买平撤单：减少平仓冻结，增加持仓；
        // 3.卖开（更新卖持仓）
        // 3.1 卖开委托：增加开仓冻结；
        // 3.2 卖开成交：减少开仓冻结，增加持仓、增加已开仓数；
        // 3.3 卖开撤单：减少开仓冻结；
        // 4.卖平（更新买持仓）
        // 4.1 卖平委托：减少持仓，增加平仓冻结；
        // 4.2 卖平成交：减少平仓冻结，增加已平仓数；
        // 4.3 卖平撤单：减少平仓冻结，增加持仓；
        int64_t market = pos->marker_;
        switch (oc_flag) {
        case kOcFlagOpen:
            if (order_volume > 0) {  // 开仓委托：增加开仓冻结
                pos->td_opening_volume_ +=  order_volume;
            }
            if (match_volume > 0) {  // 开仓成交：减少开仓冻结，增加持仓，增加已开仓数
                pos->td_opening_volume_  -= match_volume;
                pos->td_open_volume_ += match_volume;
            }
            if (withdraw_volume > 0) {  // 开仓撤单：减少开仓冻结
                if (pos->td_opening_volume_ > withdraw_volume) {
                    pos->td_opening_volume_ -= withdraw_volume;
                }
            }
            // 风控策略：更新当前期货类型的已开仓数和开仓冻结数之和
            if (forbid_closing_today_ && pos->marker_ == co::kMarketCFFEX) {
                string code = pos->code_;
                if (code.length() > kCFFEXOptionLength) {
                    return;
                }
                string type = code.length() > 2 ? code.substr(0, 2) : "";
                if (auto it = open_cache_.find(type); it != open_cache_.end()) {
                    if (order_volume > 0) {
                        it->second += order_volume;
                    }
                    if (withdraw_volume > 0) {
                        it->second -= order_volume;
                    }
                }
            }
            break;
        case kOcFlagClose:  // 平仓
        case kOcFlagForceClose:  // 强平
        case kOcFlagForceOff:  // 强减
        case kOcFlagLocalForceClose:  // 本地强平
            if (order_volume > 0) {
                // SHFE，INE Close相当于平昨指令, CFFEX只能平昨，不能平今
                if (market == co::kMarketSHFE || market == co::kMarketINE || market == co::kMarketCFFEX) {
                    pos->yd_closing_volume_ += order_volume;
                } else {  // 先平今，后平昨(先开先平)
                    int64_t td_available_pos = pos->GetTodayAvailableVolume();
                    if (td_available_pos >= order_volume) {
                        pos->td_closing_volume_ += order_volume;
                    } else {
                        pos->td_closing_volume_ += td_available_pos;
                        pos->yd_closing_volume_ += (order_volume - td_available_pos);
                    }
                }
            }
            if (match_volume > 0) {
                if (market == co::kMarketSHFE || market == co::kMarketINE || market == co::kMarketCFFEX) {
                    if (pos->yd_closing_volume_ >= match_volume) {
                        pos->yd_closing_volume_ -= match_volume;
                    }
                    pos->yd_close_volume_ += match_volume;
                } else {  // 先平今，后平昨(先开先平)
                    int64_t today_pos = 0, yes_today = 0;
                    if (pos->td_closing_volume_ >= match_volume) {
                        today_pos = match_volume;
                    } else {
                        today_pos = pos->td_closing_volume_;
                        yes_today = match_volume - pos->td_closing_volume_;
                    }
                    pos->td_closing_volume_ -= today_pos;
                    pos->td_close_volume_ += today_pos;
                    pos->yd_closing_volume_ -= yes_today;
                    pos->yd_close_volume_ += yes_today;
                }
            }
            if (withdraw_volume > 0) {
                if (market == co::kMarketSHFE || market == co::kMarketINE || market == co::kMarketCFFEX) {
                    if (pos->yd_closing_volume_ >= withdraw_volume) {
                        pos->yd_closing_volume_ -= withdraw_volume;
                    }
                } else {
                    int64_t today_pos = 0, yes_today = 0;
                    if (pos->td_closing_volume_ >= withdraw_volume) {
                        today_pos = withdraw_volume;
                    } else {
                        today_pos = pos->td_closing_volume_;
                        yes_today = withdraw_volume - pos->td_closing_volume_;
                    }
                    pos->td_closing_volume_ -= today_pos;
                    pos->yd_closing_volume_ -= yes_today;
                }
            }
            break;
        case kOcFlagCloseToday:  // 平今
            if (order_volume > 0) {
                pos->td_closing_volume_ += order_volume;
            }
            if (match_volume > 0) {
                if (pos->td_closing_volume_ >= match_volume) {
                    pos->td_closing_volume_ -= match_volume;
                }
                pos->td_close_volume_ += match_volume;
            }
            if (withdraw_volume > 0) {
                if (pos->td_closing_volume_ >= withdraw_volume) {
                    pos->td_closing_volume_ -= withdraw_volume;
                }
            }
            break;
        case kOcFlagCloseYesterday:  // 平昨
            if (order_volume > 0) {
                pos->yd_closing_volume_ += order_volume;
            }
            if (match_volume > 0) {
                if (pos->yd_closing_volume_ >= match_volume) {
                    pos->yd_closing_volume_ -= match_volume;
                }
                pos->yd_close_volume_ += match_volume;
            }
            if (withdraw_volume > 0) {
                if (pos->yd_closing_volume_ >= withdraw_volume) {
                    pos->yd_closing_volume_ -= withdraw_volume;
                }
            }
            break;
        default:
            break;
        }
    } else if (trade_type_ == kTradeTypeOption) {
        if (oc_flag == kOcFlagOpen) {
            if (order_volume > 0) {
                pos->td_opening_volume_ += order_volume;
            }
            if (match_volume > 0) {
                if (pos->td_opening_volume_ >= match_volume) {
                    pos->td_opening_volume_ -= match_volume;
                }
                pos->td_open_volume_ += match_volume;
            }
            if (withdraw_volume > 0) {
                if (pos->td_opening_volume_ >= withdraw_volume) {
                    pos->td_opening_volume_ -= withdraw_volume;
                }
            }
        } else if (oc_flag == kOcFlagClose) {
            if (order_volume > 0) {
                pos->td_closing_volume_ += order_volume;
            }
            if (match_volume > 0) {
                if (pos->td_closing_volume_ >= match_volume) {
                    pos->td_closing_volume_ -= match_volume;
                }
                pos->td_close_volume_ += match_volume;
            }
            if (withdraw_volume > 0) {
                if (pos->td_closing_volume_ >= withdraw_volume) {
                    pos->td_closing_volume_ -= withdraw_volume;
                }
            }
        }
    } else if (trade_type_ == kTradeTypeCredit) {
        // 只考虑 提保品卖
        if (order_volume > 0) {
            pos->yd_closing_volume_ += order_volume;
        }
        if (match_volume > 0) {
            if (pos->yd_closing_volume_ >= match_volume) {
                pos->yd_closing_volume_ -= match_volume;
            }
            pos->yd_close_volume_ += match_volume;
        }
        if (withdraw_volume > 0) {
            if (pos->yd_closing_volume_ >= withdraw_volume) {
                pos->yd_closing_volume_ -= withdraw_volume;
            }
        }
    }
}

int64_t PositionMaster::GetAutoOcFlag(int64_t bs_flag, const MemTradeOrder& order) {
    int64_t ret_oc_flag = kOcFlagOpen;  // 默认开仓
    string code = order.code;
    int64_t order_volume = order.volume;
    if (trade_type_ == kTradeTypeFuture) {
    //    中金所开平逻辑：
    //    1 有昨仓，则平
    //    2 有昨仓，且今天开过仓，则平。强制是强平指令， 也会转为开
    //    3 开仓时，检查此品种今天的开仓手数
    //
    //    上期所和INE，先开昨，后平今，区分平昨和平今
    //    1 有昨仓，则平昨
    //    2 有今仓，则平今
    //    2 今仓 + 昨仓 > order_volume，则开仓
    //
    //    CZCE和DCE，先平今，后平昨，先开先平，不区分平昨和平今
    //    1 有今仓，则平仓
    //    2 有昨仓，则平仓
    //    3 今仓 + 昨仓 > order_volume，则平仓
        int64_t market = order.market;
        if (market == 0) {
            market = co::CodeToMarket(code);
        }
        auto itr_acc = positions_.find(code);
        if (itr_acc == positions_.end()) {
            // 没有持仓，默认开仓, 如果是股指，继续检查开仓数量
            GetPosition(code, bs_flag, kOcFlagOpen);
            itr_acc = positions_.find(code);
        }
        InnerPositionPtr pos;
        if (bs_flag == kBsFlagBuy) {
            pos = itr_acc->second->second;  // 买时，先找到对应的空头
        } else {
            pos = itr_acc->second->first;   // 卖时，先找到对应的多头
        }
        int64_t yd_available_pos = pos->GetYesterdayAvailableVolume();
        int64_t td_available_pos = pos->GetTodayAvailableVolume();

        if (market == kMarketCFFEX) {
            // 风控策略：限制股指期货当日开仓数量
            string type = code.length() > 2 ? code.substr(0, 2) : "";
            int64_t open_volume = 0;  // 当前期货类型的已开仓数和开仓冻结数之和
            if (auto it = open_cache_.find(type); it != open_cache_.end()) {
                open_volume = it->second;
            }
            // 自动开平
            if (order.oc_flag == co::kOcFlagAuto) {
                if (open_volume == 0 && yd_available_pos >= order_volume) {
                    ret_oc_flag = kOcFlagClose;
                }
            } else if (order.oc_flag == kOcFlagClose) {
                // 今天开过仓, 只能开仓, 不能平仓, 平强制转化为开
                if (open_volume > 0) {
                    ret_oc_flag = kOcFlagOpen;
                }
            }
            // 最后一步，检查开仓数量
            if (max_today_opening_volume_ >= 0 && ret_oc_flag == kOcFlagOpen) {
                if (order_volume + open_volume > max_today_opening_volume_) {
                    stringstream ss;
                    ss << "[当日开仓数限制]自动开平检查失败，委托数:" << order_volume << "，已开仓:" << open_volume << "，最大开仓数限制:"
                       << max_today_opening_volume_;
                    string str = ss.str();
                    throw runtime_error(str);
                }
            }
        } else {
            if (order.oc_flag == co::kOcFlagAuto) {  // 自动开平
                if (market == co::kMarketSHFE || market == co::kMarketINE) {
                    if (yd_available_pos >= order_volume) {
                        ret_oc_flag = kOcFlagCloseYesterday;
                    } else if (td_available_pos > order_volume) {
                        ret_oc_flag = kOcFlagCloseToday; // 也可以是 kOcFlagClose
                    }
                } else {
                    if ((yd_available_pos + td_available_pos) >= order_volume) {
                        ret_oc_flag = kOcFlagClose;
                    }
                }
            } else {
                ret_oc_flag = order.oc_flag;
            }
        }
        if (order.oc_flag == co::kOcFlagAuto) {
            LOG_INFO << "[AutoOpenClose]["
                     << code
                     << ", bs_flag: " << bs_flag
                     << ", volume: " << order.volume
                     << ", oc_flag: " << order.oc_flag<< "] GetAutoOcFlag: "
                     << "ret oc_flag: " << ret_oc_flag
                     << ", " << pos->ToString();
        }
    } else if (trade_type_ == kTradeTypeOption) {
        auto itr_acc = positions_.find(order.code);
        if (itr_acc != positions_.end()) {
            InnerPositionPtr pos;
            if (bs_flag == kBsFlagBuy) {
                pos = itr_acc->second->second;  // 买时，先找到对应的空头
            } else {
                pos = itr_acc->second->first;   // 卖时，先找到对应的多头
            }
            int64_t td_available_pos = pos->GetTodayAvailableVolume();
            if (td_available_pos > order_volume) {
                ret_oc_flag = kOcFlagClose;
            }
        }
    } else if (trade_type_ == kTradeTypeCredit) {
        // 只考虑 提保品卖
        auto itr_acc = positions_.find(order.code);
        if (itr_acc != positions_.end()) {
            InnerPositionPtr pos;
            if (bs_flag == kBsFlagSell) {
                pos = itr_acc->second->first;
                int64_t yd_available_pos = pos->GetYesterdayAvailableVolume();
                if (yd_available_pos >= order_volume) {
                    ret_oc_flag = kOcFlagAuto;
                }
            }
        }
    }
    return ret_oc_flag;
}

// 平昨仓,不平今仓, 如果昨仓数量不足,就开仓
int64_t PositionMaster::GetCloseYesterdayFlag(int64_t bs_flag, const MemTradeOrder& order) {
    string code = order.code;
    int64_t market = order.market;
    if (market == 0) {
        market = co::CodeToMarket(code);
    }
    int64_t order_volume = order.volume;
    int64_t ret_oc_flag = kOcFlagOpen;  // 默认开仓
    auto itr_acc = positions_.find(code);
    if (itr_acc == positions_.end()) {
        // 没有持仓，默认开仓, 如果是股指，继续检查开仓数量
        GetPosition(code, bs_flag, kOcFlagOpen);
        itr_acc = positions_.find(code);
    }
    InnerPositionPtr pos;
    if (bs_flag == kBsFlagBuy) {
        pos = itr_acc->second->second;  // 买时，先找到对应的空头
    } else {
        pos = itr_acc->second->first;   // 卖时，先找到对应的多头
    }
    int64_t yd_available_pos = pos->GetYesterdayAvailableVolume();

    if (market == kMarketCFFEX) {
        // 风控策略：限制股指期货当日开仓数量
        string type = code.length() > 2 ? code.substr(0, 2) : "";
        int64_t open_volume = 0;  // 当前期货类型的已开仓数和开仓冻结数之和
        if (auto it = open_cache_.find(type); it != open_cache_.end()) {
            open_volume = it->second;
        }

        if (open_volume == 0 && yd_available_pos >= order_volume) {
            ret_oc_flag = kOcFlagClose;
        }
        // 最后一步，检查开仓数量
        if (max_today_opening_volume_ >= 0 && ret_oc_flag == kOcFlagOpen) {
            if (order_volume + open_volume > max_today_opening_volume_) {
                stringstream ss;
                ss << "[当日开仓数限制]自动开平检查失败，委托数:" << order_volume << "，已开仓:" << open_volume << "，最大开仓数限制:"
                   << max_today_opening_volume_;
                string str = ss.str();
                throw runtime_error(str);
            }
        }
    } else {
        if (yd_available_pos >= order_volume) {
            if (market == co::kMarketSHFE || market == co::kMarketINE) {
                ret_oc_flag = kOcFlagCloseYesterday;
            } else {
                ret_oc_flag = kOcFlagClose;
            }
        }
    }
    LOG_INFO << "[AutoOpenClose]["
             << code << ", bs_flag: " << bs_flag << "] GetAutoOcFlag: "
             << "oc_flag: " << ret_oc_flag
             << ", order_volume: " << order.volume
             << ", " << pos->ToString();
    return ret_oc_flag;
}

// 除了开仓，强平、平今、平昨、强减、本地强平都认为是平仓
InnerPositionPtr PositionMaster::GetPosition(std::string code, int64_t bs_flag, int64_t oc_flag) {
    // 买开和卖平（更新买持仓), 卖开和买平（更新卖持仓
    bool long_flag = false;
    if (trade_type_ == kTradeTypeFuture || trade_type_ == kTradeTypeOption) {
        if (bs_flag == kBsFlagBuy) {
            if (oc_flag == kOcFlagOpen) {
                long_flag = true;
            }
        } else {
            if (oc_flag == kOcFlagOpen) {
                long_flag = false;
            } else {
                long_flag = true;
            }
        }
    } else if (trade_type_ == kTradeTypeCredit) {
        // 只考虑 提保品卖
        if (oc_flag == kOcFlagAuto && bs_flag == kBsFlagSell) {
            long_flag = true;
        } else {
            return nullptr;
        }
    }

    InnerPositionPtr pos = nullptr;
    std::shared_ptr<std::pair<InnerPositionPtr, InnerPositionPtr>> pair = nullptr;
    auto itr_acc = positions_.find(code);
    if (itr_acc != positions_.end()) {
        pair = itr_acc->second;
    } else {
        InnerPositionPtr long_pos = std::make_shared<InnerPosition>(code, kBsFlagBuy);
        InnerPositionPtr short_pos = std::make_shared<InnerPosition>(code, kBsFlagSell);
        pair = std::make_shared<std::pair<InnerPositionPtr, InnerPositionPtr>>();
        pair->first = long_pos;
        pair->second = short_pos;
        positions_[code] = pair;
    }
    if (long_flag) {
        pos = pair->first;
    } else {
        pos = pair->second;
    }
    return pos;
}
}  // namespace co