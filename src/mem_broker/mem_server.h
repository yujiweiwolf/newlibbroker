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
#include "utils.h"
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
    void SendQueryAssetReq(MemQueryMessage* msg);

    void SendQueryPositionReq(MemQueryMessage* msg);

    void SendQueryKnockReq(MemQueryMessage* msg);

    void SendTradeOrderReq(MemTradeOrderMessage* msg);

    void SendTradeWithdrawReq(MemTradeWithdrawMessage* msg);

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
    void HandleQueryAssetRep(MemTradeAsset* asset);
    void HandleQueryPositionRep(MemOnRspQueryPosition* pos);
    void HandleQueryKnockRep(MemTradeKnock* knock);
    void HandleTradeOrderRep(MemTradeOrderMessage* msg);
    void HandleTradeWithdrawRep(MemTradeWithdrawMessage* msg);
    void HandleTradeKnock(MemTradeKnock* msg);

    template <typename T>
    void WriteRepPublic(T* msg, int64_t frame_type, size_t body_size);
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
    std::set<std::string> all_message_;
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
                SendTradeOrderReq(order_msg);
                break;
            }
            case kMemTypeTradeWithdrawReq: {
                auto* withdraw_msg = reinterpret_cast<MemTradeWithdrawMessage*>(body);
                SendTradeWithdrawReq(withdraw_msg);
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
            SendQueryAssetReq(&req);
            break;
        }
        case kMemTypeQueryTradePositionReq: {
            const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
            MemQueryMessage req = *msg;
            SendQueryPositionReq(&req);
            break;
        }
        case kMemTypeQueryTradeKnockReq: {
            const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
            MemQueryMessage req = *msg;
            SendQueryKnockReq(&req);
            break;
        }
        case kMemTypeTradeOrderReq: {
            const auto* msg = reinterpret_cast<const MemTradeOrderMessage*>(body);
            auto it = all_message_.find(msg->id);
            if (it == all_message_.end() && !opt_->enable_query_only()) {
                all_message_.insert(msg->id);
                size_t total_len = static_cast<size_t>(kFrameHeaderSize + body_length);
                auto* buf = new char[total_len];
                memcpy(buf, data, total_len);
                MemTradeOrderMessage* order_msg = reinterpret_cast<MemTradeOrderMessage*>(buf);
                std::string error = CheckTradeOrderMessage(order_msg);
                if (!error.empty()) {
                    strncpy(order_msg->error, error.c_str(), kMemErrorSize - 1);
                    WriteRepPublic(order_msg, kMemTypeTradeOrderRep, sizeof(MemTradeOrderMessage));
                    delete [] buf;
                } else {
                    flow_control_queue_.Push(buf);
                }
            }
            break;
        }
        case kMemTypeTradeWithdrawReq: {
            auto* msg = reinterpret_cast<const MemTradeWithdrawMessage*>(body);
            auto it = all_message_.find(msg->id);
            if (it == all_message_.end() && !opt_->enable_query_only()) {
                all_message_.insert(msg->id);
                size_t total_len = static_cast<size_t>(kFrameHeaderSize + body_length);
                auto *buf = new char[total_len];
                memcpy(buf, data, total_len);
                MemTradeWithdrawMessage* withdraw_msg = reinterpret_cast<MemTradeWithdrawMessage*>(buf);
                std::string error = CheckTradeWithdrawMessage(withdraw_msg);
                if (!error.empty()) {
                    strncpy(withdraw_msg->error, error.c_str(), kMemErrorSize - 1);
                    WriteRepPublic(withdraw_msg, kMemTypeTradeWithdrawRep, sizeof(MemTradeWithdrawMessage));
                    delete [] buf;
                } else {
                    flow_control_queue_.Push(buf);
                }
            }
            break;
        }
        case kMemTypeQueryTradeAssetRep: {
            const auto* msg = reinterpret_cast<const MemTradeAsset*>(body);
            HandleQueryAssetRep(*msg);
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
void MemBrokerServer<Broker>::SendQueryAssetReq(MemQueryMessage* msg) {
    broker_.SendQueryAssetReq(msg);
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
void MemBrokerServer<Broker>::HandleQueryAssetRep(MemTradeAsset* asset) {

}

template <typename Broker>
void MemBrokerServer<Broker>::SendQueryPositionReq(MemQueryMessage* msg) {
    broker_.SendQueryPositionReq(msg);
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
void MemBrokerServer<Broker>::HandleQueryPositionRep(MemOnRspQueryPosition* pos) {

}

template <typename Broker>
void MemBrokerServer<Broker>::SendQueryKnockReq(MemQueryMessage* msg) {
    broker_.SendQueryKnockReq(msg);
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
void MemBrokerServer<Broker>::HandleQueryKnockRep(MemTradeKnock* knock) {

}

template <typename Broker>
void MemBrokerServer<Broker>::SendTradeOrderReq(MemTradeOrderMessage* msg) {
    // 防对敲检查和超时检查
    std::string error = msg->error;
    if (error.empty()) {
        error = risk_.HandleTradeOrderReq(msg);
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
    }
    if (!error.empty()) {
        strncpy(msg->error, error.c_str(), kMemErrorSize - 1);
        WriteRepPublic(msg, kMemTypeTradeOrderRep, sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg->items_size);
        return;
    }
    // 防对敲检查通过，加入订单簿
    risk_.OnTradeOrderReqPass(msg);
    pos_master_->HandleTradeOrderReq(msg);
    broker_.SendTradeOrderReq(msg);
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
void MemBrokerServer<Broker>::HandleTradeOrderRep(MemTradeOrderMessage* msg) {
    risk_.HandleTradeOrderRep(msg);
    pos_master_->HandleTradeOrderRep(msg);
}

template <typename Broker>
void MemBrokerServer<Broker>::SendTradeWithdrawReq(MemTradeWithdrawMessage* msg) {
    // 先检查 msg 中是否已有错误（流控等前置检查已赋值）
    if (strlen(msg->error) > 0) {
        WriteRepPublic(msg, kMemTypeTradeWithdrawRep, sizeof(MemTradeWithdrawMessage));
        return;
    }
    risk_.HandleTradeWithdrawRep(msg);
    broker_.SendTradeWithdrawReq(msg);
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
void MemBrokerServer<Broker>::HandleTradeWithdrawRep(MemTradeWithdrawMessage* msg) {
    risk_.HandleTradeWithdrawRep(msg);
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
    risk_.HandleTradeKnock(msg);
    pos_master_->HandleTradeKnock(*msg);
}

// WriteRepPublic 模板：将消息写入 rep_publication_
template <typename Broker>
template <typename T>
void MemBrokerServer<Broker>::WriteRepPublic(T* msg, int64_t frame_type, size_t body_size) {
    MemFrameHeader frame{};
    frame.type = frame_type;
    frame.body_length = static_cast<int64_t>(body_size);
    size_t total_len = sizeof(MemFrameHeader) + body_size;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), msg, body_size);
    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = rep_publication_->offer(ab, 0, total_len);
    if (result < 0) {
        LOG_WARN << "WriteRepPublic republish failed, result: " << result;
    }
}

}  // namespace co
