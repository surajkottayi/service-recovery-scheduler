#include "CRecoverySchedulerStubImpl.hpp"
#include "Logger.hpp"
#include <CommonAPI/CommonAPI.hpp>
#define COMMONAPI_INTERNAL_COMPILATION
#include <CommonAPI/DBus/DBusAddress.hpp>
#include <CommonAPI/DBus/DBusClientId.hpp>
#include <CommonAPI/DBus/DBusError.hpp>
#include <CommonAPI/DBus/DBusFactory.hpp>
#include <CommonAPI/DBus/DBusInputStream.hpp>
#include <CommonAPI/DBus/DBusMessage.hpp>
#include <CommonAPI/DBus/DBusOutputStream.hpp>
#include <CommonAPI/DBus/DBusStubAdapter.hpp>
#undef COMMONAPI_INTERNAL_COMPILATION

#include <iostream>
#include <utility>

using namespace lib_srs;

std::string CRecoverySchedulerStubImpl::extractDbusUniqueName(const std::shared_ptr<CommonAPI::ClientId> &client)
{
    auto dbusClient  = std::dynamic_pointer_cast<CommonAPI::DBus::DBusClientId>(client);
    char *lDBusidCpy = nullptr;
    if (dbusClient)
    {
        const char *lDBusid = dbusClient->getDBusId();
        lDBusidCpy          = lDBusid ? strdup(lDBusid) : nullptr;
    }
    std::string result = lDBusidCpy ? std::string(lDBusidCpy) : std::string();
    free(lDBusidCpy);
    return result;
}

CRecoverySchedulerStubImpl::~CRecoverySchedulerStubImpl()
{
    if (auto lDBus = m_SignalConn.lock())
    {
        lDBus->removeSignalMemberHandler(m_SignalToken, this);
    }
}

void CRecoverySchedulerStubImpl::setCallbacks(CallbackRegister onRegister, CallbackUnregister onUnregister, CallbackFailure onFailure, CallbackReport onReport)
{
    onRegisterService    = std::move(onRegister);
    onUnregisterService  = std::move(onUnregister);
    onServiceFailure     = std::move(onFailure);
    onReportServiceState = std::move(onReport);
}

void CRecoverySchedulerStubImpl::registerService(const std::shared_ptr<CommonAPI::ClientId> client,
                                                 std::string serviceName,
                                                 std::vector<GeneratedIface::RecoveryState> recoveryActions,
                                                 int32_t recoveryInterval, registerServiceReply_t reply)
{
    // Translate the CommonAPI enum to lib_srs::RecoveryState (identical layout,
    // but distinct C++ types).
    std::vector<RecoveryState> lActions;
    lActions.reserve(recoveryActions.size());
    for (auto lAction : recoveryActions)
    {
        lActions.push_back(static_cast<RecoveryState>(static_cast<uint8_t>(lAction)));
    }
    const std::string lUniqueBusName = extractDbusUniqueName(client);
    const pid_t lPid                 = queryUnixPidForBusName(lUniqueBusName);
    bool isOk                        = onRegisterService ? onRegisterService(serviceName, lActions, static_cast<int>(recoveryInterval), lPid) : false;

    if (isOk)
    {
        if (!lUniqueBusName.empty())
        {
            std::lock_guard<std::mutex> lock(m_NameMutex);
            m_ServiceToBusName[serviceName] = PeerInfo{lUniqueBusName, lPid};
            LOG_INFO("SRSC", "DBUS", "watching bus name '" << lUniqueBusName << "' pid=" << lPid << " for service '" << serviceName << "'");
        }
    }
    reply(isOk ? GeneratedIface::RegisterResult::OK : GeneratedIface::RegisterResult::INVALID_NAME);
}

void CRecoverySchedulerStubImpl::unregisterService(const std::shared_ptr<CommonAPI::ClientId> /*client*/,
                                                   std::string serviceName, unregisterServiceReply_t reply)
{
    bool isOk = onUnregisterService ? onUnregisterService(serviceName) : false;
    if (isOk)
    {
        std::lock_guard<std::mutex> lock(m_NameMutex);
        m_ServiceToBusName.erase(serviceName);
    }
    reply(isOk ? GeneratedIface::UnregisterResult::OK : GeneratedIface::UnregisterResult::NOT_FOUND);
}

void CRecoverySchedulerStubImpl::reportServiceState(const std::shared_ptr<CommonAPI::ClientId> /*client*/,
                                                    std::string serviceName,
                                                    GeneratedIface::RecoveryState currentAction,
                                                    GeneratedIface::RecoveryState lastAction,
                                                    reportServiceStateReply_t reply)
{
    const auto lCurrent = static_cast<RecoveryState>(static_cast<uint8_t>(currentAction));
    const auto lLast    = static_cast<RecoveryState>(static_cast<uint8_t>(lastAction));
    const bool isOk     = onReportServiceState ? onReportServiceState(serviceName, lCurrent, lLast) : false;
    reply(isOk ? GeneratedIface::QueryResult::OK : GeneratedIface::QueryResult::NOT_FOUND);
}

void CRecoverySchedulerStubImpl::notifyStateChanged(const std::string &serviceName, RecoveryState actionTaken)
{
    fireServiceStateChangedEvent(serviceName, GeneratedIface::RecoveryState(static_cast<GeneratedIface::RecoveryState::Literal>(static_cast<uint8_t>(actionTaken))));
}

void CRecoverySchedulerStubImpl::onSignalDBusMessage(const CommonAPI::DBus::DBusMessage &lDBusMsg)
{
    // Currently we only subscribe to NameOwnerChanged (signature "sss") but
    // guard anyway in case more filters are added later.
    CommonAPI::DBus::DBusInputStream lInStream(lDBusMsg);
    std::string lStrName, lStrOldOwner, lStrNewOwner;
    lInStream.readValue(lStrName, static_cast<CommonAPI::EmptyDeployment *>(nullptr));
    lInStream.readValue(lStrOldOwner, static_cast<CommonAPI::EmptyDeployment *>(nullptr));
    lInStream.readValue(lStrNewOwner, static_cast<CommonAPI::EmptyDeployment *>(nullptr));
    handleNameOwnerChanged(lStrName, lStrOldOwner, lStrNewOwner);
}

void CRecoverySchedulerStubImpl::handleNameOwnerChanged(const std::string &name, const std::string & /*oldOwner*/,
                                                        const std::string &newOwner)
{
    if (newOwner.empty())
    {
        std::string lStrAffectedService;
        pid_t lPid = -1;
        {
            std::lock_guard<std::mutex> lock(m_NameMutex);
            for (auto itFind = m_ServiceToBusName.begin(); itFind != m_ServiceToBusName.end(); ++itFind)
            {
                if (itFind->second.uniqueBusName == name)
                {
                    lStrAffectedService = itFind->first;
                    lPid                = itFind->second.pid;
                    m_ServiceToBusName.erase(itFind);
                    break;
                }
            }
        }
        if (!lStrAffectedService.empty())
        {
            LOG_ERROR("SRSC", "DBUS", "peer lost service='" << lStrAffectedService << "' bus=" << name << " pid=" << lPid << " - driving recovery");
            if (onServiceFailure)
            {
                onServiceFailure(lStrAffectedService);
            }
        }
    }
}

pid_t CRecoverySchedulerStubImpl::queryUnixPidForBusName(const std::string &uniqueBusName)
{
    pid_t lPid = -1;
    auto lDBus = m_SignalConn.lock();
    if (lDBus && !uniqueBusName.empty())
    {
        // org.freedesktop.DBus.GetConnectionUnixProcessID(name) -> u
        static const CommonAPI::DBus::DBusAddress kBusAddr("org.freedesktop.DBus",
                                                           "/org/freedesktop/DBus",
                                                           "org.freedesktop.DBus");
        auto lMsg = CommonAPI::DBus::DBusMessage::createMethodCall(kBusAddr, "GetConnectionUnixProcessID", "s");
        CommonAPI::DBus::DBusOutputStream lOut(lMsg);
        lOut.writeValue(uniqueBusName, static_cast<const CommonAPI::EmptyDeployment *>(nullptr));
        lOut.flush();

        CommonAPI::DBus::DBusError lErr;
        CommonAPI::CallInfo lInfo(CommonAPI::DEFAULT_SEND_TIMEOUT_MS);
        auto lReply = lDBus->sendDBusMessageWithReplyAndBlock(lMsg, lErr, &lInfo);
        if (lReply && !lReply.isErrorType())
        {
            CommonAPI::DBus::DBusInputStream lIn(lReply);
            uint32_t lRawPid = 0;
            lIn.readValue(lRawPid, static_cast<const CommonAPI::EmptyDeployment *>(nullptr));
            lPid = static_cast<pid_t>(lRawPid);
        }
    }
    return lPid;
}

bool CRecoverySchedulerStubImpl::run(std::shared_ptr<CRecoverySchedulerStubImpl> self, const std::string &domain, const std::string &instance)
{
    bool isOk     = true;
    auto lRuntime = CommonAPI::Runtime::get();
    if (!lRuntime)
    {
        isOk = false;
        LOG_ERROR("SRSC", "CAPI", "CommonAPI::Runtime::get() returned null - cannot register service");
    }
    else
    {
        // DBusFactory::init() is guarded by isInitialized_ so it is safe to call
        // unconditionally. This corrects a static-init ordering hazard in CAPI
        // 3.2.3-r1: if Runtime::initFactories() ran before FactoryInit (the
        // INITIALIZER in DBusFactory.cpp) registered the DBus factory as the
        // defaultFactory_, Factory::init() was silently skipped and
        // stubAdapterCreateFunctions_ is empty, causing registerService to fail.
        CommonAPI::DBus::Factory::get()->init();

        if (!lRuntime->registerService(domain, instance, self))
        {
            isOk = false;
            LOG_ERROR("SRSC", "CAPI", "registerService FAILED for " << domain << ":" << instance << " - check COMMONAPI_CONFIG points at fidl/commonapi4dbus.ini and that the routing key matches the versioned CAPI address (see fidl/commonapi4dbus.ini).");
        }
    }
    if (isOk)
    {
        LOG_INFO("SRSC", "CAPI", "CommonAPI service registered " << domain << ":" << instance);

        // Subscribe to org.freedesktop.DBus.NameOwnerChanged on the very same
        // D-Bus connection CommonAPI already owns for this service.
        auto lStubAdapter = std::dynamic_pointer_cast<CommonAPI::DBus::DBusStubAdapter>(self->getStubAdapter());
        if (lStubAdapter)
        {
            auto lDBusConn = lStubAdapter->getDBusConnection();
            if (lDBusConn)
            {
                self->m_SignalConn  = lDBusConn;
                self->m_SignalToken = lDBusConn->addSignalMemberHandler("/org/freedesktop/DBus",
                                                                        "org.freedesktop.DBus",
                                                                        "NameOwnerChanged",
                                                                        "sss",
                                                                        std::weak_ptr<CommonAPI::DBus::DBusProxyConnection::DBusSignalHandler>(self),
                                                                        false);
            }
        }
    }
    return isOk;
}
