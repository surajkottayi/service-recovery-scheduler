/**
 * @file  CRecoverySchedulerStubImpl.hpp
 * @brief CommonAPI D-Bus skeleton (server-side) for the `RecoveryScheduler`
 *        interface declared in `fidl/RecoveryScheduler.fidl`.
 *
 * This header is only meaningful after the CommonAPI generators have run
 * (see `cmake/CommonAPI.cmake`). The generated header lives under:
 * ```
 *   <build>/fidl/gen/core/v1/com/bmw/recovery/RecoveryScheduler
 *                                                StubDefault.hpp
 * ```
 * @note The class also subscribes directly to
 *       `org.freedesktop.DBus.NameOwnerChanged` (via
 *       `DBusProxyConnection::DBusSignalHandler`) so it can detect when a
 *       registered client's D-Bus connection disappears. This is the
 *       primary signal path that produces the `RecoveryState::CRASHED`
 *       transition in @ref lib_srs::CRecoveryScheduler.
 */
#ifndef RecoverySchedulerStubImpl_HPP
#define RecoverySchedulerStubImpl_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

#include <v1/com/bmw/recovery/RecoveryScheduler.hpp>
#include <v1/com/bmw/recovery/RecoverySchedulerStubDefault.hpp>

// Direct inheritance from CommonAPI's DBusSignalHandler requires these
// "internal" headers. Scope the flag tightly so it doesn't leak.
#define COMMONAPI_INTERNAL_COMPILATION
#include <CommonAPI/DBus/DBusMessage.hpp>
#include <CommonAPI/DBus/DBusProxyConnection.hpp>
#undef COMMONAPI_INTERNAL_COMPILATION

#include "Common.hpp" // for lib_srs::RecoveryState (no scheduler dep)

namespace lib_srs
{
    /// Convenience alias for the CommonAPI-generated interface stub type.
    /// The generated stub lives in this deeply nested namespace.
    using GeneratedIface = ::v1::com::bmw::recovery::RecoveryScheduler;

    /**
     * @brief Server-side glue between the CommonAPI stub and the in-process
     *        @ref CRecoveryScheduler.
     *
     * The class has two orthogonal responsibilities:
     *  - implement the RPC surface defined by `RecoveryScheduler.fidl` by
     *    overriding `RecoverySchedulerStubDefault`; each override translates
     *    the request into a callback into the owning scheduler.
     *  - listen for `org.freedesktop.DBus.NameOwnerChanged` on the DBus
     *    session daemon so we can spot peers that vanished without calling
     *    `unregisterService` (i.e. crashes).
     */
    class CRecoverySchedulerStubImpl final : public ::v1::com::bmw::recovery::RecoverySchedulerStubDefault,
                                             public CommonAPI::DBus::DBusProxyConnection::DBusSignalHandler
    {
    public:
        // Callbacks the owning scheduler installs to receive IPC requests and
        // D-Bus peer-loss notifications. Keeps this class agnostic of the
        // concrete scheduler type.

        /// Callback fired for every accepted `registerService` request.
        using CallbackRegister = std::function<bool(const std::string &serviceName, const std::vector<RecoveryState> &actions, int recoveryInterval, const pid_t &pid)>;
        /// Callback fired for every accepted `unregisterService` request.
        using CallbackUnregister = std::function<bool(const std::string &serviceName)>;
        /// Callback fired when a peer's D-Bus name owner disappears (crash).
        using CallbackFailure = std::function<void(const std::string &serviceName)>;
        /// Callback fired for every `reportServiceState` update.
        using CallbackReport = std::function<bool(const std::string &serviceName, RecoveryState currentAction, RecoveryState lastAction)>;

        /**
         * @brief Extract the D-Bus unique bus name (e.g. `:1.42`) from a
         *        CommonAPI client identifier.
         * @param client CommonAPI-supplied client id passed into every stub override.
         * @return The `:x.y` unique connection name, or an empty string if
         *         the id doesn't map to a D-Bus binding.
         */
        std::string extractDbusUniqueName(const std::shared_ptr<CommonAPI::ClientId> &client);

        CRecoverySchedulerStubImpl() = default;
        ~CRecoverySchedulerStubImpl() override;

        /**
         * @brief Install the four callbacks the scheduler wants notifications on.
         *
         * Must be called once, before @ref run, otherwise incoming requests
         * will be accepted by the stub but silently dropped.
         */
        void setCallbacks(CallbackRegister onRegister, CallbackUnregister onUnregister, CallbackFailure onFailure, CallbackReport onReport);

        /**
         * @brief `RecoveryScheduler.fidl :: registerService` implementation.
         *
         * Resolves the caller's D-Bus unique name + pid, records the mapping
         * in @ref m_ServiceToBusName, and forwards to @ref onRegisterService.
         * @param client            CommonAPI-supplied caller identity.
         * @param serviceName       Fully-qualified service name to register.
         * @param recoveryActions   Ordered recovery policy.
         * @param recoveryInterval  Client-requested back-off in seconds.
         * @param reply             CommonAPI reply continuation (must be invoked exactly once).
         */
        void registerService(const std::shared_ptr<CommonAPI::ClientId> client,
                             std::string serviceName, std::vector<GeneratedIface::RecoveryState> recoveryActions,
                             int32_t recoveryInterval, registerServiceReply_t reply) override;

        /**
         * @brief `RecoveryScheduler.fidl :: unregisterService` implementation.
         *
         * Removes the peer mapping, then forwards to @ref onUnregisterService.
         */
        void unregisterService(const std::shared_ptr<CommonAPI::ClientId> client, std::string serviceName, unregisterServiceReply_t reply) override;

        /**
         * @brief `RecoveryScheduler.fidl :: reportServiceState` implementation.
         *
         * Forwards to @ref onReportServiceState after translating the
         * generated enum values to @ref lib_srs::RecoveryState.
         */
        void reportServiceState(const std::shared_ptr<CommonAPI::ClientId> client,
                                std::string serviceName,
                                GeneratedIface::RecoveryState currentAction,
                                GeneratedIface::RecoveryState lastAction,
                                reportServiceStateReply_t reply) override;

        /**
         * @brief Broadcast a `stateChanged` D-Bus signal to interested clients.
         * @param serviceName Service whose state transitioned.
         * @param actionTaken Action the scheduler chose to apply.
         */
        void notifyStateChanged(const std::string &serviceName, RecoveryState actionTaken);

        /**
         * @brief `DBusSignalHandler` callback: routes matched signals to
         *        @ref handleNameOwnerChanged.
         */
        void onSignalDBusMessage(const CommonAPI::DBus::DBusMessage &msg) override;

        /**
         * @brief Register this stub with the CommonAPI runtime and start
         *        listening for calls.
         * @param self     `shared_ptr` to `*this` (the runtime stores it).
         * @param domain   CommonAPI domain, usually `"local"`.
         * @param instance CommonAPI instance name; must match what clients use.
         * @return `true` on successful registration.
         */
        static bool run(std::shared_ptr<CRecoverySchedulerStubImpl> self, const std::string &domain = "local", const std::string &instance = "com.bmw.recovery.RecoveryScheduler");

    private:
        /**
         * @brief Handler invoked when the DBus daemon reports that a bus
         *        name changed ownership.
         *
         * If `newOwner` is empty the owning peer has disconnected: we look
         * up which registered service that unique name was backing and fire
         * the crash callback.
         */
        void handleNameOwnerChanged(const std::string &name, const std::string &oldOwner, const std::string &newOwner);

        /**
         * @brief Ask `org.freedesktop.DBus.GetConnectionUnixProcessID` for
         *        the pid behind a unique bus name.
         * @param uniqueBusName A unique connection name such as `:1.42`.
         * @return The peer's pid, or `-1` if the daemon can't answer.
         */
        pid_t queryUnixPidForBusName(const std::string &uniqueBusName);

        /// Per-peer identity we cache at register time so we can attribute
        /// a later `NameOwnerChanged` back to the right service.
        struct PeerInfo
        {
            std::string uniqueBusName; ///< e.g. ":1.42"
            pid_t pid{-1};             ///< OS pid of the connected peer
        };

        std::mutex m_NameMutex;                                                       ///< Guards @ref m_ServiceToBusName.
        std::unordered_map<std::string, PeerInfo> m_ServiceToBusName;                 ///< serviceName -> peer info.
        std::weak_ptr<CommonAPI::DBus::DBusProxyConnection> m_SignalConn;             ///< Connection we subscribed on (kept weak to avoid a cycle).
        CommonAPI::DBus::DBusProxyConnection::DBusSignalHandlerToken m_SignalToken{}; ///< Token for the `NameOwnerChanged` subscription; used to unregister in the dtor.

        CallbackRegister onRegisterService;     ///< Set by @ref setCallbacks.
        CallbackUnregister onUnregisterService; ///< Set by @ref setCallbacks.
        CallbackFailure onServiceFailure;       ///< Set by @ref setCallbacks.
        CallbackReport onReportServiceState;    ///< Set by @ref setCallbacks.
    };
} // namespace lib_srs

#endif // CRecoverySchedulerStubImpl
