// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "Aeron.h"
#include "FragmentAssembler.h"
#include "mem_struct.h"
#include "flow_control.h"
#include "anti_self_knock_risker.h"
#include "position_master.h"
#include <cstdint>
using namespace aeron;
const int64_t INNER_AERON_STREAM_ID = 2003;

namespace co {

template <typename Broker>
class MemBrokerServer {
 public:
    MemBrokerServer() : risk_() {}
    ~MemBrokerServer() {}

    void Init(MemBrokerOptionsPtr option);
    void Start();
    void Run();

    // 具体的broker需要实现的函数
    void SendQueryTradeAsset(MemQueryMessage* msg);

    void SendQueryTradePosition(MemQueryMessage* msg);

    void SendQueryTradeKnock(MemQueryMessage* msg);

    void SendTradeOrder(MemTradeOrderMessage* msg);

    void SendTradeWithdraw(MemTradeWithdrawMessage* msg);

    // 具体的broker,得到柜台的数据后直接调用
    void OnRspQryAsset(MemTradeAsset* asset);
    void OnRspQryPosition(MemOnRspQueryPosition* pos);
    void OnRspQryKnock(MemTradeKnock* knock);
    void OnRspTradeOrder(MemTradeOrderMessage* msg);
    void OnRspTradeWithdraw(MemTradeWithdrawMessage* msg);
    void SendTradeKnock(MemTradeKnock* msg);
    void InitQueryPosition();

 private:
    void HandleMessage(const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header& header);

    // 从inner_publication中读到信息的处理函数
    void HandleRspQryAsset(MemTradeAsset* asset);
    void HandleRspQryPosition(MemOnRspQueryPosition* pos);
    void HandleRspQryKnock(MemTradeKnock* knock);
    void HandleTradeOrder(MemTradeOrderMessage* msg);
    void HandleRspTradeWithdraw(MemTradeWithdrawMessage* msg);
    void HandleTradeKnock(MemTradeKnock* msg);

    void WriteRepPublicTradeOrder(MemTradeOrderMessage* msg);
    void WriteRepPublicWithdraw(MemTradeWithdrawMessage* msg);
private:
    MemBrokerOptionsPtr opt_;
    Broker broker_;
    std::shared_ptr<Subscription> req_subscription_;
    std::shared_ptr<Publication> rep_publication_;
    std::shared_ptr<Publication> inner_publication_;
    std::shared_ptr<Subscription> inner_subscription_;
    FlowControlQueue flow_control_queue_;
    AntiSelfKnockRisk risk_;
    std::unique_ptr<PositionMaster> pos_master_;
};

// ============================================================
// Template implementation
// ============================================================

template <typename Broker>
void MemBrokerServer<Broker>::Init(MemBrokerOptionsPtr option) {
    opt_ = option;

    aeron::Context context;
    std::shared_ptr<Aeron> aeron = Aeron::connect(context);
    string aeron_channel = opt_->aeron_channel();
    int64_t req_stream_id = opt_->req_stream_id();
    int64_t rep_stream_id = opt_->rep_stream_id();

    std::int64_t req_sub_id = aeron->addSubscription(aeron_channel, req_stream_id);
    req_subscription_ = aeron->findSubscription(req_sub_id);
    while (!req_subscription_) {
        std::this_thread::yield();
        req_subscription_ = aeron->findSubscription(req_sub_id);
    }

    std::int64_t innerPubId = aeron->addPublication(aeron_channel, INNER_AERON_STREAM_ID);
    inner_publication_ = aeron->findPublication(innerPubId);
    while (!inner_publication_) {
        std::this_thread::yield();
        inner_publication_ = aeron->findPublication(innerPubId);
    }

    std::int64_t repPubId = aeron->addPublication(aeron_channel, rep_stream_id);
    rep_publication_ = aeron->findPublication(repPubId);
    while (!rep_publication_) {
        std::this_thread::yield();
        rep_publication_ = aeron->findPublication(repPubId);
    }

    std::int64_t innerSubId = aeron->addSubscription(aeron_channel, INNER_AERON_STREAM_ID);
    inner_subscription_ = aeron->findSubscription(innerSubId);
    while (!inner_subscription_) {
        std::this_thread::yield();
        inner_subscription_ = aeron->findSubscription(innerSubId);
    }

    flow_control_queue_.Init(option);
    pos_master_ = std::make_unique<PositionMaster>(option->trade_type());
    broker_.SetServer(this);
    broker_.OnInit();
}

template <typename Broker>
void MemBrokerServer<Broker>::InitQueryPosition() {
    MemQueryMessage msg {};
    string id = "INIT_" + x::UUID();
    strcpy(msg.id, id.c_str());
    broker_.OnQueryTradePosition(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::Start() {
    std::thread run_thread(&MemBrokerServer::Run, this);
    run_thread.detach();
}

template <typename Broker>
void MemBrokerServer<Broker>::Run() {
    FragmentAssembler fragmentAssembler([this](const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header& header) {
        HandleMessage(buffer, offset, length, header);
    });
    fragment_handler_t fragHandler = fragmentAssembler.handler();

    while (true) {
        while (req_subscription_->poll(fragHandler, 1) > 0) {}
        while (inner_subscription_->poll(fragHandler, 1) > 0) {}

        char* msg = flow_control_queue_.Pop();
        if (msg) {
            auto* frame = reinterpret_cast<MemFrameHeader*>(msg);
            int64_t type = frame->type;
            char* body = msg + sizeof(MemFrameHeader);

            switch (type) {
            case kMemTypeTradeOrderReq: {
                auto* order_msg = reinterpret_cast<MemTradeOrderMessage*>(body);
                SendTradeOrder(order_msg);
                break;
            }
            case kMemTypeTradeWithdrawReq: {
                auto* withdraw_msg = reinterpret_cast<MemTradeWithdrawMessage*>(body);
                SendTradeWithdraw(withdraw_msg);
                break;
            }
            default:
                LOG_WARN << "flow_control Pop unknown type: " << type;
                break;
            }

            delete[] msg;
        }
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::HandleMessage(const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header&) {
    constexpr util::index_t kFrameHeaderSize = static_cast<util::index_t>(sizeof(MemFrameHeader));
    util::index_t read_offset = 0;

    while (read_offset + kFrameHeaderSize <= length) {
        auto* data = buffer.buffer() + offset + read_offset;
        const auto* frame = reinterpret_cast<const MemFrameHeader*>(data);
        int64_t msg_type = frame->type;
        int64_t body_length = frame->body_length;

        if (read_offset + kFrameHeaderSize + body_length > length) {
            LOG_WARN << "HandleMessage body overflow: body_length：" << body_length
                     << " remaining：" << (length - read_offset);
            break;
        }

        const auto* body = data + kFrameHeaderSize;
        LOG_INFO << "HandleMessage type: " << msg_type << " body_length: " << body_length;

        switch (msg_type) {
        case kMemTypeQueryTradeAssetReq: {
            const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
            MemQueryMessage req = *msg;
            SendQueryTradeAsset(&req);
            break;
        }
        case kMemTypeQueryTradePositionReq: {
            const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
            MemQueryMessage req = *msg;
            SendQueryTradePosition(&req);
            break;
        }
        case kMemTypeQueryTradeKnockReq: {
            const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
            MemQueryMessage req = *msg;
            SendQueryTradeKnock(&req);
            break;
        }
        case kMemTypeTradeOrderReq: {
            size_t total_len = static_cast<size_t>(kFrameHeaderSize + body_length);
            auto* buf = new char[total_len];
            memcpy(buf, data, total_len);
            flow_control_queue_.Push(buf);
            break;
        }
        case kMemTypeTradeWithdrawReq: {
            size_t total_len = static_cast<size_t>(kFrameHeaderSize + body_length);
            auto* buf = new char[total_len];
            memcpy(buf, data, total_len);
            flow_control_queue_.Push(buf);
            break;
        }
        default:
            LOG_WARN << "HandleMessage unknown msg_type：" << msg_type;
            break;
        }
        read_offset += (sizeof(MemFrameHeader) + body_length);
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::SendQueryTradeAsset(MemQueryMessage* msg) {
    broker_.SendQueryTradeAsset(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::OnRspQryAsset(MemTradeAsset* asset) {
    LOG_INFO << "OnRspQryAsset " << ToString(asset);

    size_t body_len = sizeof(MemTradeAsset);
    MemFrameHeader frame{};
    frame.type = kMemTypeQueryTradeAssetRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), asset, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "OnRspQryAsset inner publish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::HandleRspQryAsset(MemTradeAsset* asset) {
    OnRspQryAsset(asset);
}

template <typename Broker>
void MemBrokerServer<Broker>::SendQueryTradePosition(MemQueryMessage* msg) {
    broker_.OnQueryTradePosition(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::OnRspQryPosition(MemOnRspQueryPosition* pos) {
    LOG_INFO << "OnRspQryPosition " << ToString(&pos->item);

    size_t body_len = sizeof(MemOnRspQueryPosition);
    MemFrameHeader frame{};
    frame.type = kMemTypeQueryTradePositionRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), pos, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "OnRspQryPosition inner publish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::HandleRspQryPosition(MemOnRspQueryPosition* pos) {
    OnRspQryPosition(pos);
}

template <typename Broker>
void MemBrokerServer<Broker>::SendQueryTradeKnock(MemQueryMessage* msg) {
    broker_.OnQueryTradeKnock(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::OnRspQryKnock(MemTradeKnock* knock) {
    LOG_INFO << "OnRspQryKnock " << ToString(knock);

    size_t body_len = sizeof(MemTradeKnock);
    MemFrameHeader frame{};
    frame.type = kMemTypeQueryTradeKnockRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), knock, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "OnRspQryKnock inner publish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::HandleRspQryKnock(MemTradeKnock* knock) {
    OnRspQryKnock(knock);
}

template <typename Broker>
void MemBrokerServer<Broker>::SendTradeOrder(MemTradeOrderMessage* msg) {
    // 防对敲检查和超时检查
    std::string error = risk_.HandleTradeOrderReq(msg);
    if (error.empty()) {
         int64_t timeout = opt_->request_timeout_ms();
         if (msg->timeout > 0) {
             timeout = msg->timeout;
         }
         if (timeout > 0) {
             int64_t now = x::RawDateTime();
             int64_t time_spread = x::SubRawDateTime(now, msg->timestamp);
             if (time_spread > timeout) {
                 error = "报单超时, 实际超时: " + std::to_string(time_spread) + ", 阈值: " + std::to_string(timeout);
             }
         }
    }
    if (!error.empty()) {
        strncpy(msg->error, error.c_str(), kMemErrorSize - 1);
        WriteRepPublicTradeOrder(msg);
        return;
    }
    // 防对敲检查通过，加入订单簿
    risk_.OnTradeOrderReqPass(msg);
    pos_master_->HandleOrderReq(msg);
    broker_.SendTradeOrder(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::OnRspTradeOrder(MemTradeOrderMessage* msg) {
    LOG_INFO << "OnRspTradeOrder " << ToString(msg);

    size_t body_len = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg->items_size;
    MemFrameHeader frame{};
    frame.type = kMemTypeTradeOrderRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "OnRspTradeOrder inner publish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::SendTradeKnock(MemTradeKnock* msg) {
    size_t body_len = sizeof(MemTradeKnock);
    MemFrameHeader frame{};
    frame.type = kMemTypeTradeKnockRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "SendTradeKnock inner publish failed, result: " << result;
    }
}


template <typename Broker>
void MemBrokerServer<Broker>::HandleTradeKnock(MemTradeKnock* msg) {

}

template <typename Broker>
void MemBrokerServer<Broker>::SendTradeWithdraw(MemTradeWithdrawMessage* msg) {
    broker_.SendTradeWithdraw(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::OnRspTradeWithdraw(MemTradeWithdrawMessage* msg) {
    LOG_INFO << "OnRspTradeWithdraw " << ToString(msg);

    size_t body_len = sizeof(MemTradeWithdrawMessage);
    MemFrameHeader frame{};
    frame.type = kMemTypeTradeWithdrawRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = inner_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "OnRspTradeWithdraw inner publish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::HandleRspTradeWithdraw(MemTradeWithdrawMessage* msg) {
    OnRspTradeWithdraw(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::WriteRepPublicTradeOrder(MemTradeOrderMessage* msg) {
    size_t body_len = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg->items_size;
    MemFrameHeader frame{};
    frame.type = kMemTypeTradeOrderRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = rep_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "WriteRepPublicTradeOrder republish failed, result: " << result;
    }
}

template <typename Broker>
void MemBrokerServer<Broker>::WriteRepPublicWithdraw(MemTradeWithdrawMessage* msg) {
    size_t body_len = sizeof(MemTradeWithdrawMessage);
    MemFrameHeader frame{};
    frame.type = kMemTypeTradeWithdrawRep;
    frame.body_length = static_cast<int64_t>(body_len);
    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_len);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = rep_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "WriteRepPublicWithdraw republish failed, result: " << result;
    }
}

}  // namespace co
