#include "utils.h"
#include <regex>
#include <string>
#include <charconv>
#include <boost/lexical_cast.hpp>

namespace co {

std::string CreateStandardOrderNo(int64_t market, const std::string_view& order_no) {
    // 将市场代码添加到委托合同号中，便于流控模块识别；
    // std_order_no = <market>-<order_no>
    if (market <= 0) {
        throw std::invalid_argument("create standard order_no failed because of illegal market: " +
                                    std::to_string(market));
    }
    std::string std_order_no;
    std_order_no.reserve(12 + order_no.size());

    char buf[12];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), market);
    std_order_no.append(buf, ptr);
    std_order_no += '-';
    std_order_no += order_no;
    return std_order_no;
}

std::string ParseStandardOrderNo(char* order_no, int64_t* market) {
    // std_order_no = <market>-<real_order_no>
    try {
        int pos = 0;
        if (strlen(order_no) >= 3) {
            if (order_no[1] == '-') {
                if (order_no[0] >= '0' && order_no[0] <= '9') {
                    pos = 2;
                    (*market) = order_no[0] - '0';
                } else {
                    throw std::invalid_argument("illegal standard order_no: " + std::string(order_no));
                }
            } else if (order_no[2] == '-') {
                if ((order_no[0] >= '0' && order_no[0] <= '9') && (order_no[1] >= '0' && order_no[1] <= '9')) {
                    pos = 3;
                    (*market) = (order_no[0] - '0') * 10 + (order_no[1] - '0');
                } else {
                    throw std::invalid_argument("illegal standard order_no: " + std::string(order_no));
                }
            }
        }
        return string(order_no, pos);
    } catch (...) {
        throw std::invalid_argument("parse standard order_no failed, order_no: " + std::string(order_no));
    }
}

bool IsFlowControlRequiredMarket(int64_t market) {
    return market == kMarketSH || market == kMarketSZ || market == kMarketBJ;
}

std::string CreateInnerOrderNo(const co::fbs::TradeOrderT& order) {
    return order.order_no;
}

std::string CreateInnerMatchNo(const co::fbs::TradeKnockT& knock) {
    // 最早期的股票内部成交合同号生成规则：<order_no>_<code>_<match_no>_<match_time>
    // ETF申赎操作，一个委托合同号对应多条不同代码的成交回报数据
    // 注意：自成交match_no是一样的，系统根据match_no是否重复来检查是否有自成交。
    std::string inner_match_no;
    switch (knock.trade_type) {
        case kTradeTypeSpot: {  //
            std::stringstream ss;
            ss << knock.order_no << "_" << knock.match_no << "_" << knock.code;
            inner_match_no = ss.str();
            break;
        }
        case kTradeTypeFuture:  //
        case kTradeTypeOption:  //
        default: {
            std::stringstream ss;
            if (!knock.match_no.empty()) {
                ss << knock.bs_flag << "_" << knock.match_no;
            }
            inner_match_no = ss.str();
            break;
        }
    }
   return inner_match_no;
}

std::string CheckTradeOrderMessage(MemTradeOrderMessage *req) {
    if (req->bs_flag <= 0 || req->bs_flag >= 4) {
        return ("[FAN-BROKER-ERROR] not valid bs_flag: " + to_string(req->bs_flag));
    }
    if (req->items_size <= 0) {
        return ("[FAN-BROKER-ERROR] not valid items_size: " + to_string(req->items_size));
    }
    for (int i = 0; i < req->items_size; ++i) {
        auto order = &req->items[i];
        if (strlen(order->code) == 0) {
            return "[FAN-BROKER-ERROR] code is required";
        }
        int64_t market = order->market;
        if (market <= 0) {
            market = co::CodeToMarket(order->code);
            order->market = market;
        }
        if (market <= 0) {
            std::stringstream ss;
            return ("[FAN-BROKER-ERROR] unknown market suffix in code: " + string(order->code));
        }
        //只允许放一天的逆回购 204001.SH 131810.SZ
        if (req->bs_flag == co::kBsFlagSell) {
            if (market == kMarketSH) {
                if ((order->code[0] = '2') && (order->code[1] = '0') && (order->code[2] = '4')) {
                    if ((order->code[3] = '0') && (order->code[4] = '0') && (order->code[5] = '1')) {
                        return "";
                    } else {
                        return ("[FAN-Broker-RepoRiskError] only 1-Day repo code is allowed: " + string(order->code));
                    }
                }
            } else if (market == kMarketSZ) {
                if ((order->code[0] = '1') && (order->code[1] = '3') && (order->code[2] = '1') && (order->code[3] = '8')) {
                    if ((order->code[4] = '1') && (order->code[5] = '0')) {
                        return "";
                    } else {
                        return ("[FAN-Broker-RepoRiskError] only 1-Day repo code is allowed: " + string(order->code));
                    }
                }
            }
        }
    }
    return "";
}

std::string CheckTradeWithdrawMessage(MemTradeWithdrawMessage *req) {
    if (strlen(req->order_no) == 0 && strlen(req->batch_no) == 0) {
        return ("[FAN-BROKER-ERROR] order_no and batch_no both empty");
    }
    if (strlen(req->order_no)) {
        string order_no = req->order_no;
        if (auto it = order_no.find("_"); it == string::npos) {
            return ("[FAN-BROKER-ERROR] not valid order_no: " + order_no);
        } else {
            req->market = co::atoi(order_no.substr(0, it).c_str());
        }
    } else if (strlen(req->batch_no)) {
        string batch_no = req->batch_no;
        // batch_no 格式: 1_400_4324242 (market_ordersize_seq)
        auto first = batch_no.find('_');
        if (first == string::npos) {
            return ("[FAN-BROKER-ERROR] not valid batch_no format(no '_'): " + batch_no);
        }
        auto second = batch_no.find('_', first + 1);
        if (second == string::npos) {
            return ("[FAN-BROKER-ERROR] not valid batch_no format(only one '_'): " + batch_no);
        }
        req->market = co::atoi(batch_no.substr(0, first).c_str());
    }
    return "";
}

void* map_file(const char* filepath, size_t size) {
    // 1. 先判断文件是否存在
    struct stat st;
    int exists = (stat(filepath, &st) == 0);

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // 2. 确保文件长度足够存放映射内容
    {
        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("fstat");
            close(fd);
            exit(EXIT_FAILURE);
        }
        if (st.st_size < (off_t) size) {
            // 扩展文件到 size 字节
            if (ftruncate(fd, size) == -1) {
                perror("ftruncate");
                close(fd);
                exit(EXIT_FAILURE);
            }
        }
    }

    // 可读可写，且修改会写回原文件
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return MAP_FAILED;
    }
    close(fd);
    if (!exists) {
        // 文件是新创建的：整个区域清零
        memset(addr, 0, size);
    }
    return addr;
}

}