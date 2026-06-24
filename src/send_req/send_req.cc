#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <regex>
#include "x/x.h"
#include "coral/coral.h"
#include "Aeron.h"
#include "../mem_broker/mem_struct.h"

using namespace co;
using namespace aeron;
namespace po = boost::program_options;

const char fund_id[] = "S1";
const string AERON_CHANNEL = "aeron:ipc";
const int64_t AERON_REQ_STREAM_ID = 1001;
const int64_t AERON_REP_STREAM_ID = 1002;

void order_sh(Publication& publication) {
    MemTradeOrderMessage msg = {};
    strcpy(msg.id, x::UUID().c_str());
    strcpy(msg.fund_id, fund_id);
    msg.items_size = 1;
    msg.bs_flag = co::kBsFlagBuy;
    msg.timestamp = x::RawDateTime();
    strcpy(msg.items[0].code, "600000.SH");
    msg.items[0].market = kMarketSH;
    msg.items[0].volume = 100;
    msg.items[0].price = 9.99;

    string volume_input;
    cout << "please input volume" << endl;
    cin >> volume_input;
    msg.items[0].volume = atoll(volume_input.c_str());

    size_t body_len = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg.items_size;

    MemFrameHeader frame{};
    frame.type = kMemTypeTradeOrderReq;
    frame.body_length = static_cast<int64_t>(body_len);

    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), &msg, body_len);

    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = publication.offer(ab, 0, total_len);
    LOG_INFO << "send order_sh, code: " << msg.items[0].code
             << ", volume: " << msg.items[0].volume
             << ", price: " << msg.items[0].price
             << ", result: " << result;
}

void withdraw(Publication& publication) {
    MemTradeWithdrawMessage msg = {};
    strcpy(msg.id, x::UUID().c_str());
    strcpy(msg.fund_id, fund_id);
    cout << "please input order_no" << endl;
    cin >> msg.order_no;
    msg.timestamp = x::RawDateTime();

    MemFrameHeader frame{};
    frame.type = kMemTypeTradeWithdrawReq;
    frame.body_length = sizeof(MemTradeWithdrawMessage);

    size_t total_len = sizeof(MemFrameHeader) + sizeof(MemTradeWithdrawMessage);
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), &msg, sizeof(MemTradeWithdrawMessage));

    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = publication.offer(ab, 0, total_len);
    LOG_INFO << "send withdraw, fund_id: " << msg.fund_id
             << ", order_no: " << msg.order_no
             << ", result: " << result;
}

void order(Publication& publication) {
    MemTradeOrderMessage msg = {};
    strcpy(msg.id, x::UUID().c_str());
    strcpy(msg.fund_id, fund_id);
    msg.items_size = 1;
    msg.bs_flag = co::kBsFlagBuy;
    msg.timestamp = x::RawDateTime();

    int64_t bs_flag = 0;
    {
        getchar();
        cout << "please input : BUY 600000.SH 100 9.9\n";
        std::string input;
        getline(std::cin, input);
        std::cout << "your input is: # " << input << " #" << std::endl;
        std::smatch result;
        if (regex_match(input, result, std::regex("^(BUY|SELL|CREATE|REDEEM) ([0-9]{1,10})\.(SH|SZ) ([0-9]{1,10}) ([.0-9]{1,10})$")))
        {
            string command = result[1].str();
            string instrument = result[2].str();
            string market = result[3].str();
            string volume_str = result[4].str();
            string price_str = result[5].str();
            if (command == "BUY") {
                bs_flag = kBsFlagBuy;
            } else if (command == "SELL") {
                bs_flag = kBsFlagSell;
            }

            string code;
            if (market == "SH") {
                msg.items[0].market = kMarketSH;
                code = instrument + ".SH";
            } else if (market == "SZ") {
                msg.items[0].market = kMarketSZ;
                code = instrument + ".SZ";
            }
            strcpy(msg.items[0].code, code.c_str());
            msg.items[0].volume = atoll(volume_str.c_str());
            msg.items[0].price = atof(price_str.c_str());
        }
        LOG_INFO << "send order, code: " << msg.items[0].code
                 << ", volume: " << msg.items[0].volume
                 << ", price: " << msg.items[0].price;
    }
    msg.bs_flag = bs_flag;

    size_t body_len = sizeof(MemTradeOrderMessage) + sizeof(MemTradeOrder) * msg.items_size;

    MemFrameHeader frame{};
    frame.type = kMemTypeTradeOrderReq;
    frame.body_length = static_cast<int64_t>(body_len);

    size_t total_len = sizeof(MemFrameHeader) + body_len;
    std::vector<std::uint8_t> buffer(total_len, 0);
    memcpy(buffer.data(), &frame, sizeof(MemFrameHeader));
    memcpy(buffer.data() + sizeof(MemFrameHeader), &msg, body_len);

    AtomicBuffer ab(buffer.data(), buffer.size());
    std::int64_t result = publication.offer(ab, 0, total_len);
    LOG_INFO << "send order result: " << result;
}

int main(int argc, char* argv[]) {
    try {
        aeron::Context context;
        std::shared_ptr<Aeron> aeron = Aeron::connect(context);

        // 创建 Publication（请求通道）
        std::int64_t pubId = aeron->addPublication(AERON_CHANNEL, AERON_REQ_STREAM_ID);
        std::shared_ptr<Publication> publication = aeron->findPublication(pubId);
        while (!publication) {
            std::this_thread::yield();
            publication = aeron->findPublication(pubId);
        }

        // 等待 Subscriber 连接
        std::cout << "Waiting for subscriber..." << std::endl;
        while (!publication->isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Subscriber connected!" << std::endl;

        string usage("\nTYPE  'q' to quit program\n");
        usage += "      '1' to order_sh\n";
        usage += "      '3' to withdraw\n";
        usage += "      '4' to order\n";
        cerr << (usage);

        char c;
        while ((c = getchar()) != 'q') {
            switch (c) {
            case '1': {
                order_sh(*publication);
                break;
            }
            case '3': {
                withdraw(*publication);
                break;
            }
            case '4': {
                order(*publication);
                break;
            }
            default:
                break;
            }
        }
    } catch (std::exception& e) {
        LOG_FATAL << "server is crashed, " << e.what();
        throw e;
    }
    return 0;
}
