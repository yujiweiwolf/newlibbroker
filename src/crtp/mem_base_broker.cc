// Copyright 2025 Fancapital Inc.  All rights reserved.
#include "mem_base_broker.h"


namespace co {
//// 模板基类的实现
//template <typename Derived>
//MemBaseBrokerT<Derived>::MemBaseBrokerT() {
//}
//
//template <typename Derived>
//MemBaseBrokerT<Derived>::~MemBaseBrokerT() {
//}

//template <typename Derived>
//void MemBaseBrokerT<Derived>::Init(const MemBrokerOptions& opt) {
//    try {
//        LOG_INFO << "initialize broker ...";
//        enable_stock_short_selling_ = opt.enable_stock_short_selling();
//        request_timeout_ms_ = opt.request_timeout_ms();
//        OnInit();
//        LOG_INFO << "initialize broker ok";
//    } catch (std::exception& e) {
//        LOG_INFO << "initialize broker failed: " << e.what();
//    }
//}



//template <typename Derived>
//void MemBaseBrokerT<Derived>::SendTradeOrder(MemUnionMessage* msg) {
//    auto req = &msg->order;
//    try {
//        OnTradeOrder(msg);
//    } catch (std::exception & e) {
//        std::string error = e.what();
//        if (error.empty()) {
//            error = "[FAN-BROKER-ERROR] EmptyError";
//        }
//        memcpy(msg->order.error, error.c_str(), sizeof(req->error));
//        msg->msg_type = kMemTypeTradeOrderRep;
//        SendRtnMessage(msg);
//    }
//}



//template <typename Derived>
//void MemBaseBrokerT<Derived>::SendRtnMessage(MemUnionMessage* msg) {
//    LOG_INFO <<  "SendRtnMessage";
//}


//template <typename Derived>
//void MemBaseBrokerT<Derived>::OnInit() {
//    // 静态多态调用派生类的实现
//    static_cast<Derived*>(this)->OnInit();
//}


//template <typename Derived>
//void MemBaseBrokerT<Derived>::OnTradeOrder(MemUnionMessage* msg) {
//    // 静态多态调用派生类的实现
//    static_cast<Derived*>(this)->OnTradeOrder(msg);
//}
}  // namespace co

