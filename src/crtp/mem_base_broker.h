// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "../mem_broker/options.h"
#include "../mem_broker/mem_struct.h"

namespace co {

// 静态多态基类，使用CRTP模式
template<typename Derived>
class MemBaseBrokerT {
public:
    MemBaseBrokerT() {};

    ~MemBaseBrokerT() {};

    void Init(const MemBrokerOptions &opt) {
        try {
            LOG_INFO << "initialize broker ...";
            OnInit();
            LOG_INFO << "initialize broker ok";
        } catch (std::exception& e) {
            LOG_INFO << "initialize broker failed: " << e.what();
        }
    }

    void SendTradeOrder(MemUnionMessage *msg) {
        LOG_INFO << __FUNCTION__;
        OnTradeOrder(msg);
    }

    void SendRtnMessage(MemUnionMessage *msg) {
        LOG_INFO << __FUNCTION__;
    }


protected:
    virtual void OnInit() {
        static_cast<Derived*>(this)->OnInit();
    }

    virtual void OnTradeOrder(MemUnionMessage *msg) {
        static_cast<Derived*>(this)->OnTradeOrder(msg);
    }
};
}
// namespace co

