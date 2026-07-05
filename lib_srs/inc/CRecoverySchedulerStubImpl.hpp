#ifndef RecoverySchedulerStubImpl_HPP
#define RecoverySchedulerStubImpl_HPP

// CommonAPI D-Bus skeleton (server-side) for the RecoveryScheduler
// interface defined in fidl/RecoveryScheduler.fidl.
//
// This header is only meaningful after the CommonAPI generators have run
// (see cmake/CommonAPI.cmake). The generated header lives under:
//   <build>/fidl/gen/core/v1/com/bmw/recovery/RecoveryScheduler
//                                                StubDefault.hpp

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
#include <CommonAPI/DBus/DBusProxyConnection.hpp>
#include <CommonAPI/DBus/DBusMessage.hpp>
#undef COMMONAPI_INTERNAL_COMPILATION

#include "Common.hpp" // for lib_srs::RecoveryState (no scheduler dep)

namespace lib_srs
{
    // The generated stub lives in this deeply nested namespace.
    using GeneratedIface = ::v1::com::bmw::recovery::RecoveryScheduler;

    // Bridges CommonAPI IPC calls to the in-process CRecoveryScheduler and
    // also handles org.freedesktop.DBus.NameOwnerChanged directly so it can
    // detect when a registered client's D-Bus connection disappears.
    class CRecoverySchedulerStubImpl final : public ::v1::com::bmw::recovery::RecoverySchedulerStubDefault,
                                             public CommonAPI::DBus::DBusProxyConnection::DBusSignalHandler
    {
    public:
        // Callbacks the owning scheduler installs to receive IPC requests and
        // D-Bus peer-loss notifications. Keeps this class agnostic of the
        // concrete scheduler type.
        using CallbackRegister = std::function<bool(const std::string &serviceName, const std::vector<RecoveryState> &actions, int recoveryInterval)>;
        using CallbackUnregister = std::function<bool(const std::string &serviceName)>;
        using CallbackFailure = std::function<void(const std::string &serviceName)>;

        std::string extractDbusUniqueName(const std::shared_ptr<CommonAPI::ClientId> &client);

        CRecoverySchedulerStubImpl() = default;
        ~CRecoverySchedulerStubImpl() override;

        void setCallbacks(CallbackRegister onRegister, CallbackUnregister onUnregister, CallbackFailure onFailure);

        void registerService(const std::shared_ptr<CommonAPI::ClientId> client,
                             std::string serviceName, std::vector<GeneratedIface::RecoveryState> recoveryActions,
                             int32_t recoveryInterval, registerServiceReply_t reply) override;

        void unregisterService(const std::shared_ptr<CommonAPI::ClientId> client, std::string serviceName, unregisterServiceReply_t reply) override;

        void notifyStateChanged(const std::string &serviceName, RecoveryState actionTaken);

        void onSignalDBusMessage(const CommonAPI::DBus::DBusMessage &msg) override;

        static bool run(std::shared_ptr<CRecoverySchedulerStubImpl> self, const std::string &domain = "local", const std::string &instance = "com.bmw.recovery.RecoveryScheduler");

    private:
        void handleNameOwnerChanged(const std::string &name, const std::string &oldOwner, const std::string &newOwner);

        // Ask org.freedesktop.DBus.GetConnectionUnixProcessID for the pid
        // behind a unique bus name. Returns -1 if the daemon can't answer.
        pid_t queryUnixPidForBusName(const std::string &uniqueBusName);

        struct PeerInfo
        {
            std::string uniqueBusName; // e.g. ":1.42"
            pid_t pid{-1};             // OS pid of the connected peer
        };

        std::mutex m_NameMutex;
        std::unordered_map<std::string, PeerInfo> m_ServiceToBusName; // serviceName -> peer info
        std::weak_ptr<CommonAPI::DBus::DBusProxyConnection> m_SignalConn;
        CommonAPI::DBus::DBusProxyConnection::DBusSignalHandlerToken m_SignalToken{};

        CallbackRegister onRegisterService;
        CallbackUnregister onUnregisterService;
        CallbackFailure onServiceFailure;
    };
} // namespace lib_srs

#endif // CRecoverySchedulerStubImpl
