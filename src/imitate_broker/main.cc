#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <regex>
#include "imitate_broker.h"
#include "config.h"
using namespace std;
using namespace co;
namespace po = boost::program_options;

const char fund_id[] = "S1";
int main(int argc, char* argv[]) {
    try {
        MemBrokerOptionsPtr options = co::Config::Instance()->options();
        MemBrokerServer<TestBroker> server;
        server.Init(options);
        {
            MemQueryMessage msg = {};
            msg.timestamp = x::RawDateTime();
            strcpy(msg.fund_id, fund_id);
            strcpy(msg.id, x::UUID().c_str());
            server.SendQueryAssetReq(&msg);
        }

        {
            int64_t items_size = 100;
            size_t length = sizeof(co::MemTradeOrderMessage) + sizeof(co::MemTradeOrder) * items_size;
            std::vector<std::uint8_t> buffer(length, 0);
            auto *msg = reinterpret_cast<co::MemTradeOrderMessage *>(buffer.data());
            msg->items_size = items_size;
            strcpy(msg->id, x::UUID().c_str());
            msg->timestamp = x::RawDateTime();

            for (int j = 0; j < msg->items_size; j++) {
                auto order = &msg->items[j];
                sprintf(order->code, "%06d.SZ", j + 1);
                order->bs_flag = 1;
                order->volume = 100 * (j + 1);
                order->price = 0.1 * (j + 1);
            }
            msg->timeout = x::SteadyUnixNano();
            server.SendTradeOrderReq(msg);
        }
        // x::Sleep(10000);
    } catch (std::exception& e) {
        LOG_FATAL << "server is crashed, " << e.what();
        throw e;
    }
    return 0;
}
