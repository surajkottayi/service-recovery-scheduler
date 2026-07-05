#include "CRecoveryScheduler.hpp"

using namespace lib_srs;

std::shared_ptr<CRecoveryScheduler> CRecoveryScheduler::getInstance()
{
    static std::shared_ptr<CRecoveryScheduler> instance(new CRecoveryScheduler());
    return instance;
}
void CRecoveryScheduler::startSignalMonitor()
{

    if (m_FutSigMonitor.valid())
    {
        std::cout << "Signal monitor already running." << std::endl;
    }
    else
    {
        // Block SIGCHLD so only sigtimedwait() consumes it (inherited by watcher).
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);
        m_IsRunning.store(true);
        m_FutSigMonitor = std::async(std::launch::async, &CRecoveryScheduler::signalMonitor, this);
        std::cout << "Signal monitor started." << std::endl;
    }
}

void CRecoveryScheduler::signalMonitor()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    const timespec timeout{1, 0}; // 1 s wake-up to re-check m_IsRunning

    while (true)
    {
    }

    std::cout << "Signal monitor stopped." << std::endl;
}

void CRecoveryScheduler::stopSignalMonitor()
{
    m_IsRunning.store(false);
    if (m_FutSigMonitor.valid())
    {
        m_FutSigMonitor.get();
    }
}

void CRecoveryScheduler::onServiceFailure(const std::string &serviceName)
{
    std::cout << "Service failure detected for: " << serviceName << std::endl;

    std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
    auto itFind = std::find_if(m_MapServiceInfo.begin(), m_MapServiceInfo.end(),
                               [&](const auto &kv)
                               { return kv.second.serviceName == serviceName; });
    if (itFind != m_MapServiceInfo.end())
    {
        SServiceRecoveryInfo &info = itFind->second;
        ++info.recoveryActionCount;
        if (info.recoveryActionCount < static_cast<int8_t>(info.recoveryActions.size()))
        {
            const RecoveryState lNextState = info.recoveryActions[static_cast<size_t>(info.recoveryActionCount)];
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
}
bool CRecoveryScheduler::registerService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval)
{
    pid_t pid; // TODO : to find PID
    bool isNameValid = false;
    std::cout << "registerService " << serviceName << " pid=" << pid << std::endl;

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
            lServiceInfo.push(recoveryActions);
        else
            lServiceInfo.push({RecoveryState::RESTART, RecoveryState::RESTART,
                               RecoveryState::STOP, RecoveryState::DISABLE});

        {
            std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
            m_MapServiceInfo[pid] = std::move(lServiceInfo);
        }
        std::cout << "Service registered: " << serviceName << " pid=" << pid << std::endl;
    }
    else
    {
        std::cout << "Invalid service name: " << serviceName << std::endl;
        isNameValid = false;
    }
    return isNameValid;
}
bool CRecoveryScheduler::unregisterService(const std::string &serviceName)
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
        m_MapServiceInfo.erase(itFind);
        isSuccess = true;
    }
    return isSuccess;
}

void CRecoveryScheduler::init()
{
}