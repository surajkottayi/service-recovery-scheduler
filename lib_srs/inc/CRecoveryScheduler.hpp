#ifndef CRecoveryScheduler_HPP
#define CRecoveryScheduler_HPP

#include <unordered_map>
#include <sys/types.h>
#include <sys/wait.h>
#include <filesystem>
#include <Common.hpp>
#include <algorithm>
#include <pthread.h>
#include <iostream>
#include <csignal>
#include <fstream>
#include <memory>
#include <atomic>
#include <thread>
#include <future>
#include <mutex>
namespace fs = std::filesystem;
namespace lib_srs
{
    class CRecoverySchedulerStubImpl; // fwd-decl: full type in CRecoverySchedulerStubImpl.hpp

    typedef struct _SServiceRecoveryInfo
    {
        std::string serviceName;
        pid_t pid{-1}; // OS process id of the monitored service
        std::vector<RecoveryState> recoveryActions;
        int recoveryInterval; // in seconds
        int8_t recoveryActionCount;
        RecoveryState currentRecoveryState;
        void push(const std::vector<RecoveryState> &lvActions)
        {
            for (const auto &action : lvActions)
            {
                recoveryActions.push_back(action);
            }
        }
    } SServiceRecoveryInfo;

    class CRecoveryScheduler
    {
    public:
        [[nodiscard]] static std::shared_ptr<CRecoveryScheduler> getInstance();
        ~CRecoveryScheduler(); // out-of-line: shared_ptr to incomplete type

        bool onRegisterService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval = -1);
        bool onUnregisterService(const std::string &serviceName);
        void init();
        void run();
        void startSignalMonitor();

        // Public so external monitors (SIGCHLD watcher, D-Bus NameOwnerChanged
        // watcher) can trigger the recovery flow for a registered service.
        void onServiceFailure(const std::string &serviceName);

    private:
        CRecoveryScheduler(); // out-of-line: shared_ptr to incomplete type
        CRecoveryScheduler(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler &operator=(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler(CRecoveryScheduler &&) = delete;
        CRecoveryScheduler &operator=(CRecoveryScheduler &&) = delete;

        void signalMonitor();
        void stopSignalMonitor();

        std::unordered_map<std::string, SServiceRecoveryInfo> m_MapServiceInfo;
        std::mutex m_MutxServiceInfo;

        std::thread m_watcherThread;
        std::atomic_bool m_IsRunning{false};
        std::future<void> m_FutSigMonitor;

        // Exposes the scheduler on the session bus via CommonAPI so that
        // AppA/B/C can register themselves remotely.
        std::shared_ptr<CRecoverySchedulerStubImpl> m_StubImpl;
    };

} // namespace lib_srs

#endif // CRecoveryScheduler_HPP