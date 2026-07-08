/**
 * @file  CRecoveryScheduler.hpp
 * @brief In-process, thread-safe registry of monitored services and their
 *        recovery policies.
 *
 * The scheduler owns the authoritative state: which peers have registered,
 * what actions they asked for, and how many times they have crashed. IPC is
 * delegated to @ref lib_srs::CRecoverySchedulerStubImpl (the CommonAPI
 * D-Bus skeleton) which invokes the `on*` entry points via callbacks.
 */
#ifndef CRecoveryScheduler_HPP
#define CRecoveryScheduler_HPP

#include <Common.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace fs = std::filesystem;
namespace lib_srs
{
    class CRecoverySchedulerStubImpl; // fwd-decl: full type in CRecoverySchedulerStubImpl.hpp

    /**
     * @brief Per-service book-keeping tracked by the scheduler.
     *
     * One instance is stored per registered service name in
     * @ref CRecoveryScheduler::m_MapServiceInfo. Fields are mutated under
     * @ref CRecoveryScheduler::m_MutxServiceInfo.
     */
    typedef struct _SServiceRecoveryInfo
    {
        std::string serviceName;                                    ///< CommonAPI service name (matches the D-Bus well-known name).
        pid_t pid{-1};                                              ///< OS process id of the monitored service (`-1` if unknown).
        std::vector<RecoveryState> recoveryActions;                 ///< Ordered policy: action `i` is applied on the i-th recovery attempt.
        int recoveryInterval;                                       ///< Client-requested inter-attempt back-off, seconds.
        int8_t recoveryActionCount;                                 ///< Cursor into @ref recoveryActions; also count of attempts taken.
        uint32_t crashCount{0};                                     ///< Total crashes observed since first registration.
        bool isOnline{false};                                       ///< Liveness flag driven by `NameOwnerChanged`.
        RecoveryState currentRecoveryState{RecoveryState::UNKNOWN}; ///< Action currently being executed by the peer, if any.
        RecoveryState lastAction{RecoveryState::UNKNOWN};           ///< Last successfully reported action (or CRASHED after peer loss).

        /**
         * @brief Append additional recovery actions to the tail of the policy.
         * @param lvActions Actions to enqueue; order is preserved.
         */
        void push(const std::vector<RecoveryState> &lvActions)
        {
            for (const auto &action : lvActions)
            {
                recoveryActions.push_back(action);
            }
        }
    } SServiceRecoveryInfo;

    /**
     * @brief Immutable snapshot returned by @ref CRecoveryScheduler::getServiceState.
     *
     * Copyable, lock-free view of the parts of @ref SServiceRecoveryInfo that
     * external callers (CLI, IPC reply path) actually care about.
     */
    typedef struct _SServiceSnapshot
    {
        bool found{false};                                   ///< False if the service name was never registered.
        bool isOnline{false};                                ///< Whether the D-Bus peer is currently connected.
        RecoveryState lastAction{RecoveryState::UNKNOWN};    ///< Most recent action the peer executed (or CRASHED).
        RecoveryState currentAction{RecoveryState::UNKNOWN}; ///< Action in flight right now, if any.
        RecoveryState nextAction{RecoveryState::UNKNOWN};    ///< Action that will be tried on the next failure.
        int attemptCount{0};                                 ///< Number of recovery attempts / crashes observed so far.
    } SServiceSnapshot;

    /**
     * @brief Central singleton that owns the recovery state machine.
     *
     * The class is `final` and non-copyable / non-movable; obtain the shared
     * instance via @ref getInstance. All public methods are safe to call from
     * multiple threads.
     */
    class CRecoveryScheduler final
    {
    public:
        /**
         * @brief Returns the process-wide scheduler instance.
         *
         * The instance is lazily constructed on first call and destroyed at
         * program shutdown. The returned `shared_ptr` may be safely stored by
         * callers that need to extend its lifetime.
         */
        [[nodiscard]] static std::shared_ptr<CRecoveryScheduler> getInstance();

        /// Out-of-line: needed because we hold a `shared_ptr` to an incomplete type.
        ~CRecoveryScheduler() = default;

        /**
         * @brief Register (or extend the policy of) a monitored service.
         * @param serviceName      Fully-qualified CommonAPI service name.
         * @param recoveryActions  Ordered recovery policy; capped at @ref MAX_RECOVERY_ACTIONS.
         * @param recoveryInterval Client-requested back-off in seconds; `-1` uses the default.
         * @param pid              OS pid of the calling peer, or `-1` if unknown.
         * @retval true  Service was accepted (either freshly added or its policy extended).
         * @retval false Rejected (e.g. name too long, policy list empty).
         */
        bool onRegisterService(const std::string &serviceName, const std::vector<RecoveryState> &recoveryActions, int recoveryInterval = -1, const pid_t &pid = -1);

        /**
         * @brief Explicit unregister request from a peer.
         * @param serviceName Service to drop from the registry.
         * @retval true  Entry was found and removed.
         * @retval false No entry with that name.
         */
        bool onUnregisterService(const std::string &serviceName);

        /**
         * @brief Notification that a peer has just applied a recovery action.
         * @param serviceName   Service reporting progress.
         * @param currentAction Action currently being executed.
         * @param lastAction    Action just completed.
         * @return `true` if the report was accepted, `false` if the service is unknown.
         */
        bool onReportServiceState(const std::string &serviceName, RecoveryState currentAction, RecoveryState lastAction);

        /**
         * @brief Take a lock-free snapshot of a service's current state.
         * @param serviceName Service to query.
         * @return Populated @ref SServiceSnapshot; `found == false` if the
         *         name was never registered.
         */
        SServiceSnapshot getServiceState(const std::string &serviceName);

        /// One-time bootstrap: brings up the CommonAPI stub and wires callbacks.
        void init();
        /**
         * @brief Register the CommonAPI stub with the runtime.
         * @return `true` if registration succeeded; `false` if the runtime
         *         was unavailable or the DBus binding refused to publish the
         *         service name (see server log for the specific reason).
         *         Callers should treat `false` as fatal.
         */
        bool run();

        /**
         * @brief Called by the stub when a peer's D-Bus connection disappears.
         *
         * Marks the peer CRASHED, bumps @ref SServiceRecoveryInfo::crashCount
         * and advances the recovery cursor so the next failure applies the
         * next queued action.
         */
        void onServiceFailure(const std::string &serviceName);

    private:
        CRecoveryScheduler(); ///< Out-of-line: `shared_ptr` to incomplete type.
        CRecoveryScheduler(const CRecoveryScheduler &)            = delete;
        CRecoveryScheduler &operator=(const CRecoveryScheduler &) = delete;
        CRecoveryScheduler(CRecoveryScheduler &&)                 = delete;
        CRecoveryScheduler &operator=(CRecoveryScheduler &&)      = delete;

        std::unordered_map<std::string, SServiceRecoveryInfo> m_MapServiceInfo; ///< serviceName -> tracked info.
        std::mutex m_MutxServiceInfo;                                           ///< Guards @ref m_MapServiceInfo.
        std::shared_ptr<CRecoverySchedulerStubImpl> m_StubImpl;                 ///< CommonAPI skeleton; owns the D-Bus connection.
    };

} // namespace lib_srs

#endif // CRecoveryScheduler_HPP