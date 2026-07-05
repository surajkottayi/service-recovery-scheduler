#include "CRecoveryScheduler.hpp"
using namespace lib_srs;

std::shared_ptr<CRecoveryScheduler> CRecoveryScheduler::getInstance()
{
    static std::shared_ptr<CRecoveryScheduler> instance(new CRecoveryScheduler());
    return instance;
}