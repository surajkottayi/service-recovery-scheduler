#include "CRecoveryScheduler.hpp"
#include "CRecoverySchedulerStubImpl.hpp"
#include "Logger.hpp"
using namespace lib_srs;

CRecoveryScheduler::CRecoveryScheduler() : m_StubImpl(std::make_shared<CRecoverySchedulerStubImpl>())
{
}

std::shared_ptr<CRecoveryScheduler> CRecoveryScheduler::getInstance()
{
    static std::shared_ptr<CRecoveryScheduler> instance(new CRecoveryScheduler());
    return instance;
}
void CRecoveryScheduler::onServiceFailure(const std::string &serviceName)
{
    LOG_WARN("SRSC", "CORE", "service failure detected: " << serviceName);

    std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
    auto itFind = std::find_if(m_MapServiceInfo.begin(), m_MapServiceInfo.end(),
                               [&](const auto &kv)
                               { return kv.second.serviceName == serviceName; });
    if (itFind != m_MapServiceInfo.end())
    {
        SServiceRecoveryInfo &info = itFind->second;
        ++info.recoveryActionCount;
        info.recoveryActionCount = (info.recoveryActionCount == static_cast<int8_t>(info.recoveryActions.size())) ? 0 : info.recoveryActionCount; // wrap around to the first action
        const RecoveryState lNextState = info.recoveryActions[static_cast<size_t>(info.recoveryActionCount)];
        LOG_DEBUG("SRSC", "CORE", "attempt=" << static_cast<int>(info.recoveryActionCount) << " nextAction=" << toString(lNextState));

        switch (lNextState)
        {
        case RecoveryState::RESTART:
            // TODO: trigger restart of serviceName (e.g. via sd_bus or execve)
            break;
        case RecoveryState::STOP:
            // TODO: stop the service
            break;
        case RecoveryState::DISABLE:
            // TODO: disable the service so it is not restarted again
            break;
        default:
            break;
        }
    }
}
bool CRecoveryScheduler::onRegisterService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval, const pid_t &pid)
{
    bool isNameValid = false;
    LOG_DEBUG("SRSC", "CORE", "registerService req name=" << serviceName << " pid=" << pid);

    auto itFind = m_MapServiceInfo.find(serviceName);
    if (itFind == m_MapServiceInfo.end())
    {

        std::for_each(g_MapServiceNames.begin(), g_MapServiceNames.end(), [&](const auto &pair)
                      {
                    if (pair.second == serviceName)
                    {
                        isNameValid = true;
                    } });
        if (isNameValid)
        {

            SServiceRecoveryInfo lServiceInfo;
            lServiceInfo.serviceName = serviceName;
            // lServiceInfo.pid = findPidByName(serviceName);
            lServiceInfo.recoveryInterval = recoveryInterval;
            lServiceInfo.recoveryActionCount = -1;

            if (!recoveryActions.empty())
            {
                lServiceInfo.push(recoveryActions);
            }
            else
            {
                // default values
                lServiceInfo.push({RecoveryState::RESTART, RecoveryState::RESTART,
                                   RecoveryState::STOP, RecoveryState::DISABLE});
            }

            std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
            auto itFindMap = m_MapServiceInfo.find(serviceName);
            if (itFindMap != m_MapServiceInfo.end())
            {
                itFindMap->second = std::move(lServiceInfo);
            }
            LOG_INFO("SRSC", "CORE", "service registered: " << serviceName << " pid=" << pid);
        }
        else
        {
            LOG_ERROR("SRSC", "CORE", "invalid service name: " << serviceName);
            isNameValid = false;
        }
    }
    else
    {
        isNameValid = true;
        LOG_WARN("SRSC", "CORE", "already registered service: " << serviceName);
    }
    return isNameValid;
}
bool CRecoveryScheduler::onUnregisterService(const std::string &serviceName)
{
    bool isSuccess = false;
    std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
    auto itFind = std::find_if(m_MapServiceInfo.begin(), m_MapServiceInfo.end(),
                               [&](const auto &kv)
                               { return kv.second.serviceName == serviceName; });
    if (itFind == m_MapServiceInfo.end())
    {
        isSuccess = false;
    }
    else
    {
        isSuccess = true;
        m_MapServiceInfo.erase(itFind);
    }
    return isSuccess;
}

void CRecoveryScheduler::init()
{
    m_StubImpl->setCallbacks([this](const std::string &lStrName, const std::vector<RecoveryState> &lvActions, int liInterval, const pid_t &pid)
                             { return this->onRegisterService(lStrName, lvActions, liInterval, pid); },
                             [this](const std::string &lStrName)
                             { return this->onUnregisterService(lStrName); },
                             [this](const std::string &lStrName)
                             { this->onServiceFailure(lStrName); });
}

void CRecoveryScheduler::run()
{
    CRecoverySchedulerStubImpl::run(m_StubImpl);
}
