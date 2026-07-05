#include "CRecoveryScheduler.hpp"
using namespace lib_srs;

int main()
{
    auto lRecoveryScheduler = CRecoveryScheduler::getInstance();
    lRecoveryScheduler->init();
    lRecoveryScheduler->startSignalMonitor();
    std::cout<< "Registering services..." << std::endl;
    lRecoveryScheduler->registerService("AppA", {RecoveryState::RESTART, RecoveryState::STOP, RecoveryState::DISABLE});
    lRecoveryScheduler->registerService("AppB", {RecoveryState::RESTART, RecoveryState::STOP});
    lRecoveryScheduler->registerService("AppC", {RecoveryState::RESTART});


    return 0;
}