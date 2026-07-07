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
        // Peer connection is gone. Record CRASHED as the last observed event
        // so a later query (even after the app restarts) can see it.
        info.isOnline = false;
        ++info.crashCount;
        ++info.recoveryActionCount;
        info.recoveryActionCount = (info.recoveryActionCount == static_cast<int8_t>(info.recoveryActions.size())) ? 0 : info.recoveryActionCount; // wrap around to the first action
        const RecoveryState lNextState = info.recoveryActions[static_cast<size_t>(info.recoveryActionCount)];
        LOG_DEBUG("SRSC", "CORE", "attempt=" << static_cast<int>(info.recoveryActionCount) << " nextAction=" << toString(lNextState));

        info.lastAction = RecoveryState::CRASHED;
        info.currentRecoveryState = lNextState;

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
            lServiceInfo.isOnline = true;

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
            m_MapServiceInfo.emplace(serviceName, std::move(lServiceInfo));
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
        // Re-registration after a crash: peer is back online.
        std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
        itFind->second.isOnline = true;
        LOG_WARN("SRSC", "CORE", "already registered service (now back online): " << serviceName);
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

bool CRecoveryScheduler::onReportServiceState(const std::string &serviceName, RecoveryState currentAction, RecoveryState lastAction)
{
    std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
    auto itFind = m_MapServiceInfo.find(serviceName);
    if (itFind == m_MapServiceInfo.end())
    {
        LOG_WARN("SRSC", "CORE", "reportServiceState for unknown service: " << serviceName);
        return false;
    }
    itFind->second.currentRecoveryState = currentAction;
    // UNKNOWN from the app means "I don't have a meaningful lastAction" — keep
    // whatever the scheduler already tracks (e.g. CRASHED set on peer loss).
    if (lastAction != RecoveryState::UNKNOWN)
    {
        itFind->second.lastAction = lastAction;
    }
    LOG_INFO("SRSC", "CORE", "state pushed name=" << serviceName << " current=" << toString(currentAction) << " last=" << toString(lastAction));
    return true;
}

SServiceSnapshot CRecoveryScheduler::getServiceState(const std::string &serviceName)
{
    SServiceSnapshot lSnapshot;
    std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
    auto itFind = m_MapServiceInfo.find(serviceName);
    if (itFind != m_MapServiceInfo.end())
    {
        const SServiceRecoveryInfo &info = itFind->second;
        lSnapshot.found = true;
        lSnapshot.isOnline = info.isOnline;
        lSnapshot.currentAction = info.currentRecoveryState;
        lSnapshot.lastAction = info.lastAction;
        lSnapshot.attemptCount = static_cast<int>(info.crashCount);
        // Peek at the action the scheduler would drive on the *next* failure.
        if (!info.recoveryActions.empty())
        {
            const size_t lSize = info.recoveryActions.size();
            const int8_t lNextIdx = (info.recoveryActionCount + 1 >= static_cast<int8_t>(lSize)) ? 0 : static_cast<int8_t>(info.recoveryActionCount + 1);
            lSnapshot.nextAction = info.recoveryActions[static_cast<size_t>(lNextIdx)];
        }
    }
    return lSnapshot;
}

void CRecoveryScheduler::init()
{
    m_StubImpl->setCallbacks([this](const std::string &lStrName, const std::vector<RecoveryState> &lvActions, int liInterval, const pid_t &pid)
                             { return this->onRegisterService(lStrName, lvActions, liInterval, pid); },
                             [this](const std::string &lStrName)
                             { return this->onUnregisterService(lStrName); },
                             [this](const std::string &lStrName)
                             { this->onServiceFailure(lStrName); },
                             [this](const std::string &lStrName, RecoveryState lCurrent, RecoveryState lLast)
                             { return this->onReportServiceState(lStrName, lCurrent, lLast); });
}

void CRecoveryScheduler::run()
{
    CRecoverySchedulerStubImpl::run(m_StubImpl);
}
