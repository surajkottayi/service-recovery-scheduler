#include "CRecoveryScheduler.hpp"
#include "CServiceRecoverySchedulerStubImpl.hpp"

#include <chrono>
#include <thread>

using namespace lib_srs;

int main()
{
    auto lRecoveryScheduler = CRecoveryScheduler::getInstance();
    lRecoveryScheduler->init();

    // Expose the scheduler on the session bus via CommonAPI so that AppA/B/C
    // can register themselves remotely.
    auto lService = std::make_shared<CServiceRecoverySchedulerStubImpl>(lRecoveryScheduler);
    CServiceRecoverySchedulerStubImpl::run(lService);

    // Block main forever — the CommonAPI runtime and the signal-monitor thread
    // do the actual work in the background.
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}