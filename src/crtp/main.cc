#include "imitate_broker.h"
using namespace std;
using namespace co;

int main(int argc, char* argv[]) {
    try {
        TestBroker broker;
        MemBrokerOptions opt;
        broker.Init(opt);
        MemTradeOrderMessage msg;
        broker.SendTradeOrder(&msg);
    } catch (std::exception& e) {
        LOG_FATAL << "server is crashed, " << e.what();
        throw e;
    }
    return 0;
}
// namespace co
