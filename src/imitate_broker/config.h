// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <map>
#include <memory>
#include <x/x.h>
#include "../mem_broker/options.h"
#include "../mem_broker/mem_struct.h"
//#include "../risker/risk_options.h"

using namespace std;

namespace co {
class Config {
 public:
    static Config* Instance();

    MemBrokerOptionsPtr options() {
        return options_;
    }

    const MemTradeAccount& account() const {
        return account_;
    }

 protected:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    const Config& operator=(const Config&) = delete;

    void Init();

 private:
    static Config* instance_;
    MemBrokerOptionsPtr options_;
    MemTradeAccount account_;
};
}  // namespace co
