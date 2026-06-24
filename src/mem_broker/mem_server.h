// Copyright 2025 Fancapital Inc.  All rights reserved.
#pragma once
#include "x/x.h"
#include "coral/coral.h"
#include "options.h"
#include "Aeron.h"
#include "FragmentAssembler.h"
#include "mem_struct.h"
using namespace aeron;

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
        string AERON_CHANNEL = opt_->aeron_channel();
        int64_t AERON_STREAM_ID = opt_->req_stream_id();

        std::int64_t subId = aeron->addSubscription(AERON_CHANNEL, AERON_STREAM_ID);
        std::shared_ptr<Subscription> subscription = aeron->findSubscription(subId);

        while (!subscription) {
            std::this_thread::yield();
            subscription = aeron->findSubscription(subId);
        }

        std::cout << "Waiting for publisher..." << std::endl;
        while (!subscription->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Publisher connected!" << std::endl;

        FragmentAssembler fragmentAssembler([this](const AtomicBuffer& buffer, util::index_t offset, util::index_t length, const Header& header) {
            HandleMessage(buffer, offset, length, header);
        });
        fragment_handler_t fragHandler = fragmentAssembler.handler();

        while (true) {
            const int fragmentsRead = subscription->poll(fragHandler, 10);
        }
    }

    void SendQueryTradeAsset(MemQueryMessage* msg) {
        broker_.SendQueryTradeAsset(msg);
    }

    void OnRspQryAsset(MemTradeAsset* asset) {
        LOG_INFO << "OnRspQryAsset " << ToString(asset);
    }

    void SendTradeOrder(MemTradeOrderMessage* msg) {
        broker_.SendTradeOrder(msg);
    }

    void OnRspTradeOrder(MemTradeOrderMessage* msg) {
        LOG_INFO << "OnRspTradeOrder " << ToString(msg);
        for (int i = 0; i < msg->items_size; ++i) {
            auto order = msg->items[i];
            LOG_INFO << ToString(&order);
        }
    }

    void SendTradeWithdraw(MemTradeWithdrawMessage* msg) {
        broker_.SendTradeWithdraw(msg);
    }

    void OnRspTradeWithdraw(MemTradeWithdrawMessage* msg) {
        LOG_INFO << "OnRspTradeWithdraw " << ToString(msg); 
    }

 private:
    MemBrokerOptionsPtr opt_;
    Broker broker_;

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
                broker_.SendQueryTradeAsset(&req);
                break;
            }
            case kMemTypeQueryTradeKnockReq: {
                const auto* msg = reinterpret_cast<const MemQueryMessage*>(body);
                MemQueryMessage req = *msg;
                broker_.SendQueryTradeAsset(&req);
                break;
            }
            default:
                LOG_WARN << "HandleMessage unknown msg_type：" << msg_type;
                break;
            }
            read_offset += (sizeof(MemFrameHeader) + body_length);

        }
    }
};

}  // namespace co
