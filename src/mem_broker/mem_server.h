// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "Aeron.h"
#include "FragmentAssembler.h"
#include "mem_struct.h"
using namespace aeron;
const int64_t INNER_AERON_STREAM_ID = 2003;

namespace co {

template <typename Broker>
class MemBrokerServer {
 public:
    MemBrokerServer() {}

    ~MemBrokerServer() {}

    void Init(MemBrokerOptionsPtr option) {
        opt_ = option;
        broker_.SetServer(this);
        broker_.OnInit();

        aeron::Context context;
        std::shared_ptr<Aeron> aeron = Aeron::connect(context);
        // signal(SIGINT, sigIntHandler);
        string aeron_channel = opt_->aeron_channel();
        int64_t req_stream_id = opt_->req_stream_id();
        int64_t rep_stream_id = opt_->rep_stream_id();

        std::int64_t req_sub_id = aeron->addSubscription(aeron_channel, req_stream_id);
        req_subscription_ = aeron->findSubscription(req_sub_id);
        
        while (!req_subscription_) {
            std::this_thread::yield();
            req_subscription_ = aeron->findSubscription(req_sub_id);
        }


        // 创建内部流 Publication（用于写入响应）
        std::int64_t innerPubId = aeron->addPublication(aeron_channel, INNER_AERON_STREAM_ID);
        inner_publication_ = aeron->findPublication(innerPubId);
        while (!inner_publication_) {
            std::this_thread::yield();
            inner_publication_ = aeron->findPublication(innerPubId);
        }

        // 创建应答流 Publication（供外部订阅 rep_stream）
        std::int64_t repPubId = aeron->addPublication(aeron_channel, rep_stream_id);
        rep_publication_ = aeron->findPublication(repPubId);
        while (!rep_publication_) {
            std::this_thread::yield();
            rep_publication_ = aeron->findPublication(repPubId);
        }

        // 创建内部流 Subscription（用于读取响应回传）
        std::int64_t innerSubId = aeron->addSubscription(aeron_channel, INNER_AERON_STREAM_ID);
        inner_subscription_ = aeron->findSubscription(innerSubId);
        while (!inner_subscription_) {
            std::this_thread::yield();
            inner_subscription_ = aeron->findSubscription(innerSubId);
        }
    }

    void Start() {
        std::thread run_thread(&MemBrokerServer::Run, this);
        run_thread.detach();
    }

    void Run() {
        FragmentAssembler fragmentAssembler([this](const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header& header) {
            HandleMessage(buffer, offset, length, header);
        });
        fragment_handler_t fragHandler = fragmentAssembler.handler();

        while (true) {
            // 先读完所有请求消息
            while (req_subscription_->poll(fragHandler, 1) > 0) {}
            // 再读完所有内部流消息
            while (inner_subscription_->poll(fragHandler, 1) > 0) {}
        }
    }

    void SendQueryTradeAsset(MemQueryMessage* msg) {
        broker_.SendQueryTradeAsset(msg);
    }

    void OnRspQryAsset(MemTradeAsset* asset) {
        LOG_INFO << "OnRspQryAsset " << ToString(asset);
    }
    void SendQueryTradePosition(MemQueryMessage* msg) {
        broker_.OnQueryTradePosition(msg);
    }

    void OnRspQryPosition(MemTradePosition* pos) {
        LOG_INFO << "OnRspQryPosition " << ToString(pos);
    }

    void SendQueryTradeKnock(MemQueryMessage* msg) {
        broker_.OnQueryTradeKnock(msg);
    }

    void OnRspQryKnock(MemTradeKnock* knock) {
        LOG_INFO << "OnRspQryKnock " << ToString(knock);
    }

    void SendTradeOrder(MemTradeOrderMessage* msg) {
        broker_.SendTradeOrder(msg);
    }

    void OnRspTradeOrder(MemTradeOrderMessage* msg) {
        LOG_INFO << "OnRspTradeOrder " << ToString(msg);

        // 写入内部流，供 Init 中的内部 Subscription 回读
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

    void SendTradeWithdraw(MemTradeWithdrawMessage* msg) {
        broker_.SendTradeWithdraw(msg);
    }

    void OnRspTradeWithdraw(MemTradeWithdrawMessage* msg) {
        LOG_INFO << "OnRspTradeWithdraw " << ToString(msg); 
    }

private:

    void HandleMessage(const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header&) {
        constexpr util::index_t kFrameHeaderSize = static_cast<util::index_t>(sizeof(MemFrameHeader));
        util::index_t read_offset = 0;

        while (read_offset + kFrameHeaderSize <= length) {
            auto* data = buffer.buffer() + offset + read_offset;
            const auto* frame = reinterpret_cast<const MemFrameHeader*>(data);
            int64_t msg_type = frame->type;
            int64_t body_length = frame->body_length;

            // 越界保护：frame + body 超出 buffer 范围时终止
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
                // MemTradeOrderMessage 是变长结构（尾部 items[]），不能值拷贝，直接传 body 指针
                auto* msg = reinterpret_cast<MemTradeOrderMessage*>(data + kFrameHeaderSize);
                SendTradeOrder(msg);
                break;
            }
            case kMemTypeTradeWithdrawReq: {
                const auto* msg = reinterpret_cast<const MemTradeWithdrawMessage*>(body);
                MemTradeWithdrawMessage req = *msg;
                SendTradeWithdraw(&req);
                break;
            }
            case kMemTypeTradeOrderRep: {
                // 内部流回读的报单响应，转发到 rep_stream 供外部订阅
                size_t total_len = static_cast<size_t>(kFrameHeaderSize + body_length);
                AtomicBuffer ab(data, total_len);
                std::int64_t result = rep_publication_->offer(ab, 0, total_len);
                if (result < 0) {
                    LOG_WARN << "kMemTypeTradeOrderRep republish failed, result: " << result;
                }
                break;
            }
            default:
                LOG_WARN << "HandleMessage unknown msg_type：" << msg_type;
                break;
            }
            read_offset += (sizeof(MemFrameHeader) + body_length);

        }
    }

 private:
    MemBrokerOptionsPtr opt_;
    Broker broker_;
    std::shared_ptr<Subscription> req_subscription_;
    std::shared_ptr<Publication> inner_publication_;
    std::shared_ptr<Subscription> inner_subscription_;
    std::shared_ptr<Publication> rep_publication_;
};
}  // namespace co
