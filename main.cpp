#include "CRecoveryScheduler.hpp"
#include "CRecoverySchedulerStubImpl.hpp"

#include <chrono>
#include <thread>

using namespace lib_srs;

int main()
{
    auto lRecoveryScheduler = CRecoveryScheduler::getInstance();
    lRecoveryScheduler->init();

    lRecoveryScheduler->run();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}