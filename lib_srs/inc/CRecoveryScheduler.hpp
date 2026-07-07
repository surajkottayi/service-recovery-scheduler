#ifndef CRecoveryScheduler_HPP
#define CRecoveryScheduler_HPP

#include <unordered_map>
#include <filesystem>
#include <Common.hpp>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
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
        uint32_t crashCount{0}; // total crashes observed since first registration
        bool isOnline{false};
        RecoveryState currentRecoveryState{RecoveryState::UNKNOWN};
        RecoveryState lastAction{RecoveryState::UNKNOWN};
        void push(const std::vector<RecoveryState> &lvActions)
        {
            for (const auto &action : lvActions)
            {
                recoveryActions.push_back(action);
            }
        }
    } SServiceRecoveryInfo;

    typedef struct _SServiceSnapshot
    {
        bool found{false};
        bool isOnline{false};
        RecoveryState lastAction{RecoveryState::UNKNOWN};
        RecoveryState currentAction{RecoveryState::UNKNOWN};
        RecoveryState nextAction{RecoveryState::UNKNOWN};
        int attemptCount{0};
    } SServiceSnapshot;
    class CRecoveryScheduler final
    {
    public:
        [[nodiscard]] static std::shared_ptr<CRecoveryScheduler> getInstance();
        ~CRecoveryScheduler() = default; // out-of-line: shared_ptr to incomplete type

        bool onRegisterService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval = -1, const pid_t &pid = -1);
        bool onUnregisterService(const std::string &serviceName);
        bool onReportServiceState(const std::string &serviceName, RecoveryState currentAction, RecoveryState lastAction);
        SServiceSnapshot getServiceState(const std::string &serviceName);
        void init();
        void run();
        void onServiceFailure(const std::string &serviceName);

    private:
        CRecoveryScheduler(); // out-of-line: shared_ptr to incomplete type
        CRecoveryScheduler(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler &operator=(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler(CRecoveryScheduler &&) = delete;
        CRecoveryScheduler &operator=(CRecoveryScheduler &&) = delete;

        std::unordered_map<std::string, SServiceRecoveryInfo> m_MapServiceInfo;
        std::mutex m_MutxServiceInfo;
        std::shared_ptr<CRecoverySchedulerStubImpl> m_StubImpl;
    };

} // namespace lib_srs

#endif // CRecoveryScheduler_HPP