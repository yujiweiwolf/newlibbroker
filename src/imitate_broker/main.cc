#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <regex>
#include "imitate_broker.h"
#include "config.h"
using namespace std;
using namespace co;
namespace po = boost::program_options;

const char fund_id[] = "S1";
string mem_dir;
string mem_req_file;
string mem_rep_file;

int main(int argc, char* argv[]) {
    try {
        MemBrokerOptionsPtr options = co::Config::Instance()->options();
        MemBrokerServer<TestBroker> server;
        server.Init(options);
        MemUnionMessage msg;
        msg.asset = {};
        server.SendQueryTradeAsset(&msg);
    } catch (std::exception& e) {
        LOG_FATAL << "server is crashed, " << e.what();
        throw e;
    }
    return 0;
}
