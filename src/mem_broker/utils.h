// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <string>
#include <random>
#include <algorithm>
#include "options.h"
#include <utility>
#include "utils.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include "x/x.h"
#include "coral/coral.h"
#include "mem_struct.h"
namespace fs = std::filesystem;

namespace co {
constexpr int64_t kMaxBodyBytes = 64 << 20; // 最大网络消息大小，64MB
std::string CreateStandardOrderNo(int64_t market, const std::string_view& order_no);
std::string ParseStandardOrderNo(char* order_no, int64_t* market = nullptr);
bool IsFlowControlRequiredMarket(int64_t market);

std::string CreateInnerOrderNo(const co::fbs::TradeOrderT& order);
std::string CreateInnerMatchNo(const co::fbs::TradeKnockT& knock);
int64_t CreateOrderStatus(const co::fbs::TradeOrderT& order);
std::string CheckTradeOrderMessage(MemTradeOrderMessage *req);
std::string CheckTradeWithdrawMessage(MemTradeWithdrawMessage *req, int64_t trade_type);

std::string GenerateRandomString(size_t length);
void* map_file(const char* filepath, size_t size);
}  // namespace co