// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include <utility>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include "x/x.h"
#include "coral/coral.h"
#include "mem_base_broker.h"


using namespace std;
namespace co {
class TestBroker: public MemBaseBrokerT<TestBroker> {
 public:
    TestBroker() = default;
    ~TestBroker() = default;
    void OnInit();
    void OnTradeOrder(MemUnionMessage* msg);

};
}  // namespace co

