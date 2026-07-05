#include "CRecoveryScheduler.hpp"
#include "CRecoverySchedulerStubImpl.hpp"

using namespace lib_srs;

CRecoveryScheduler::CRecoveryScheduler() : m_StubImpl(std::make_shared<CRecoverySchedulerStubImpl>())
{
}

CRecoveryScheduler::~CRecoveryScheduler() = default;

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
        info.recoveryActionCount = (info.recoveryActionCount == static_cast<int8_t>(info.recoveryActions.size())) ? 0 : info.recoveryActionCount; // wrap around to the first action
        const RecoveryState lNextState = info.recoveryActions[static_cast<size_t>(info.recoveryActionCount)];
        std::cout << "info.recoveryActionCount " << static_cast<int>(info.recoveryActionCount) << std::endl;
        std::cout << "lNextState " << toString(lNextState) << std::endl;

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
bool CRecoveryScheduler::onRegisterService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval)
{
    pid_t pid; // TODO : to find PID
    bool isNameValid = false;
    std::cout << "registerService " << serviceName << " pid=" << pid << std::endl;

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
                lServiceInfo.push(recoveryActions);
            else
                lServiceInfo.push({RecoveryState::RESTART, RecoveryState::RESTART,
                                   RecoveryState::STOP, RecoveryState::DISABLE});

            {
                std::lock_guard<std::mutex> lock(m_MutxServiceInfo);
                m_MapServiceInfo[serviceName] = std::move(lServiceInfo);
            }
            std::cout << "Service registered: " << serviceName << " pid=" << pid << std::endl;
        }
        else
        {
            std::cout << "Invalid service name: " << serviceName << std::endl;
            isNameValid = false;
        }
    }
    else
    {
        isNameValid = true;
        std::cout << "Already registered service: " << serviceName << std::endl;
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
        m_MapServiceInfo.erase(itFind);
        isSuccess = true;
    }
    return isSuccess;
}

void CRecoveryScheduler::init()
{
    // Route IPC requests coming from the CommonAPI skeleton back into the
    // scheduler through function pointers, so the stub stays agnostic of us.
    m_StubImpl->setCallbacks([this](const std::string &lStrName, const std::vector<RecoveryState> &lvActions, int liInterval)
                             { return this->onRegisterService(lStrName, lvActions, liInterval); },
                             [this](const std::string &lStrName)
                             { return this->onUnregisterService(lStrName); },
                             [this](const std::string &lStrName)
                             { this->onServiceFailure(lStrName); });
}

void CRecoveryScheduler::run()
{
    CRecoverySchedulerStubImpl::run(m_StubImpl);
}
