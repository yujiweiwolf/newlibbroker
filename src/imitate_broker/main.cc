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
        MemQueryMessage msg = {};
        msg.timestamp = x::RawDateTime();
        strcpy(msg.fund_id, fund_id);
        strcpy(msg.id, x::UUID().c_str());
        server.SendQueryTradeAsset(&msg);
    } catch (std::exception& e) {
        LOG_FATAL << "server is crashed, " << e.what();
        throw e;
    }
    return 0;
}
