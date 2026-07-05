#ifndef ServiceRecoverySchedulerStubImpl_HPP
#define ServiceRecoverySchedulerStubImpl_HPP

// CommonAPI D-Bus skeleton (server-side) for the ServiceRecoveryScheduler
// interface defined in fidl/ServiceRecoveryScheduler.fidl.
//
// This header is only meaningful after the CommonAPI generators have run
// (see cmake/CommonAPI.cmake). The generated header lives under:
//   <build>/fidl/gen/core/v1/com/bmw/recovery/ServiceRecoveryScheduler
//                                                StubDefault.hpp

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <v1/com/bmw/recovery/ServiceRecoveryScheduler.hpp>
#include <v1/com/bmw/recovery/ServiceRecoverySchedulerStubDefault.hpp>

// Direct inheritance from CommonAPI's DBusSignalHandler requires these
// "internal" headers. Scope the flag tightly so it doesn't leak.
#define COMMONAPI_INTERNAL_COMPILATION
#include <CommonAPI/DBus/DBusProxyConnection.hpp>
#include <CommonAPI/DBus/DBusMessage.hpp>
#undef COMMONAPI_INTERNAL_COMPILATION

#include "CRecoveryScheduler.hpp"

namespace lib_srs
{
    // The generated stub lives in this deeply nested namespace.
    using GeneratedIface = ::v1::com::bmw::recovery::ServiceRecoveryScheduler;

    // Bridges CommonAPI IPC calls to the in-process CRecoveryScheduler and
    // also handles org.freedesktop.DBus.NameOwnerChanged directly so it can
    // detect when a registered client's D-Bus connection disappears.
    class CServiceRecoverySchedulerStubImpl final : public ::v1::com::bmw::recovery::ServiceRecoverySchedulerStubDefault,
                                                    public CommonAPI::DBus::DBusProxyConnection::DBusSignalHandler
    {
    public:
        explicit CServiceRecoverySchedulerStubImpl(std::shared_ptr<CRecoveryScheduler> scheduler);
        ~CServiceRecoverySchedulerStubImpl() override;

        void registerService(const std::shared_ptr<CommonAPI::ClientId> client,
                             std::string serviceName, std::vector<GeneratedIface::RecoveryState> recoveryActions,
                             int32_t recoveryInterval, registerServiceReply_t reply) override;

        void unregisterService(const std::shared_ptr<CommonAPI::ClientId> client,
                               std::string serviceName, unregisterServiceReply_t reply) override;

        // Called by CRecoveryScheduler on failure recovery. Fires the
        // serviceStateChanged broadcast so subscribed clients get notified.
        void notifyStateChanged(const std::string &serviceName, RecoveryState actionTaken);

        // CommonAPI dispatches every subscribed D-Bus signal into this method.
        void onSignalDBusMessage(const CommonAPI::DBus::DBusMessage &msg) override;

        // Register this skeleton on the session bus. Returns false on failure.
        static bool run(std::shared_ptr<CServiceRecoverySchedulerStubImpl> self, const std::string &domain = "local", const std::string &instance = "com.bmw.recovery.ServiceRecoveryScheduler");

    private:
        void handleNameOwnerChanged(const std::string &name, const std::string &oldOwner, const std::string &newOwner);

        std::shared_ptr<CRecoveryScheduler> m_Scheduler;
        std::mutex m_NameMutex;
        std::unordered_map<std::string, std::string> m_ServiceToBusName; // serviceName -> caller unique bus name
        std::weak_ptr<CommonAPI::DBus::DBusProxyConnection> m_SignalConn;
        CommonAPI::DBus::DBusProxyConnection::DBusSignalHandlerToken m_SignalToken{};
    };
} // namespace lib_srs

#endif // CServiceRecoverySchedulerStubImpl
