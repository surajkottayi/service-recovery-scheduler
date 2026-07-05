#include "CServiceRecoverySchedulerStubImpl.hpp"

#include <CommonAPI/CommonAPI.hpp>

// Additional "internal" CommonAPI headers only needed inside the .cpp.
#define COMMONAPI_INTERNAL_COMPILATION
#include <CommonAPI/DBus/DBusClientId.hpp>
#include <CommonAPI/DBus/DBusStubAdapter.hpp>
#include <CommonAPI/DBus/DBusInputStream.hpp>
#undef COMMONAPI_INTERNAL_COMPILATION

#include <iostream>
#include <utility>

using namespace lib_srs;

namespace
{
    std::string extractDbusUniqueName(const std::shared_ptr<CommonAPI::ClientId> &client)
    {
        auto dbusClient = std::dynamic_pointer_cast<CommonAPI::DBus::DBusClientId>(client);
        char *lDBusidCpy = nullptr;
        if (dbusClient)
        {
            const char *lDBusid = dbusClient->getDBusId();
            lDBusidCpy = lDBusid ? strdup(lDBusid) : nullptr;
        }
        std::string result = lDBusidCpy ? std::string(lDBusidCpy) : std::string();
        free(lDBusidCpy);
        return result;
    }
} // namespace

CServiceRecoverySchedulerStubImpl::CServiceRecoverySchedulerStubImpl(std::shared_ptr<CRecoveryScheduler> scheduler)
    : m_Scheduler(std::move(scheduler))
{
}

CServiceRecoverySchedulerStubImpl::~CServiceRecoverySchedulerStubImpl()
{
    if (auto lDBus = m_SignalConn.lock())
    {
        lDBus->removeSignalMemberHandler(m_SignalToken, this);
    }
}

void CServiceRecoverySchedulerStubImpl::registerService(const std::shared_ptr<CommonAPI::ClientId> client,
                                                        std::string serviceName,
                                                        std::vector<GeneratedIface::RecoveryState> recoveryActions,
                                                        int32_t recoveryInterval,
                                                        registerServiceReply_t reply)
{
    // Translate the CommonAPI enum to lib_srs::RecoveryState (identical layout,
    // but distinct C++ types).
    std::vector<RecoveryState> lActions;
    lActions.reserve(recoveryActions.size());
    for (auto lAction : recoveryActions)
    {
        lActions.push_back(static_cast<RecoveryState>(static_cast<uint8_t>(lAction)));
    }

    const bool isOk = m_Scheduler->registerService(serviceName, lActions, static_cast<int>(recoveryInterval));

    using R = GeneratedIface::RegisterResult;
    if (isOk)
    {
        const std::string lUniqueBusName = extractDbusUniqueName(client);
        if (!lUniqueBusName.empty())
        {
            std::lock_guard<std::mutex> lock(m_NameMutex);
            m_ServiceToBusName[serviceName] = lUniqueBusName;
            std::cout << "Watching D-Bus name '" << lUniqueBusName << "' for service '" << serviceName << "'" << std::endl;
        }
    }
    reply(isOk ? R::OK : R::INVALID_NAME);
}

void CServiceRecoverySchedulerStubImpl::unregisterService(const std::shared_ptr<CommonAPI::ClientId> /*client*/,
                                                          std::string serviceName,
                                                          unregisterServiceReply_t reply)
{
    const bool isOk = m_Scheduler->unregisterService(serviceName);
    if (isOk)
    {
        std::lock_guard<std::mutex> lock(m_NameMutex);
        m_ServiceToBusName.erase(serviceName);
    }
    reply(isOk ? GeneratedIface::UnregisterResult::OK : GeneratedIface::UnregisterResult::NOT_FOUND);
}

void CServiceRecoverySchedulerStubImpl::notifyStateChanged(const std::string &serviceName, RecoveryState actionTaken)
{
    fireServiceStateChangedEvent(serviceName, GeneratedIface::RecoveryState(static_cast<GeneratedIface::RecoveryState::Literal>(
                                                  static_cast<uint8_t>(actionTaken))));
}

void CServiceRecoverySchedulerStubImpl::onSignalDBusMessage(const CommonAPI::DBus::DBusMessage &lDBusMsg)
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

void CServiceRecoverySchedulerStubImpl::handleNameOwnerChanged(const std::string &name, const std::string & /*oldOwner*/,
                                                               const std::string &newOwner)
{
    if (newOwner.empty())
    {
        std::string lStrAffectedService;
        {
            std::lock_guard<std::mutex> lock(m_NameMutex);
            for (auto itFind = m_ServiceToBusName.begin(); itFind != m_ServiceToBusName.end(); ++itFind)
            {
                if (itFind->second == name)
                {
                    lStrAffectedService = itFind->first;
                    m_ServiceToBusName.erase(itFind);
                    break;
                }
            }
        }
        if (!lStrAffectedService.empty())
        {
            std::cout << "D-Bus peer lost for service '" << lStrAffectedService
                      << "' (bus name " << name << ") - driving recovery" << std::endl;
            m_Scheduler->onServiceFailure(lStrAffectedService);
        }
    }
}

bool CServiceRecoverySchedulerStubImpl::run(std::shared_ptr<CServiceRecoverySchedulerStubImpl> self, const std::string &domain, const std::string &instance)
{
    auto lRuntime = CommonAPI::Runtime::get();
    if (lRuntime)
    {
        if (lRuntime->registerService(domain, instance, self))
        {
            std::cout << "CommonAPI service registered: " << domain << ":" << instance << std::endl;

            // Subscribe to org.freedesktop.DBus.NameOwnerChanged on the very same
            // D-Bus connection CommonAPI already owns for this service.
            auto lStubAdapter = std::dynamic_pointer_cast<CommonAPI::DBus::DBusStubAdapter>(self->getStubAdapter());
            if (lStubAdapter)
            {
                auto lDBusConn = lStubAdapter->getDBusConnection();
                if (lDBusConn)
                {

                    self->m_SignalConn = lDBusConn;
                    self->m_SignalToken = lDBusConn->addSignalMemberHandler("/org/freedesktop/DBus",
                                                                            "org.freedesktop.DBus",
                                                                            "NameOwnerChanged",
                                                                            "sss",
                                                                            std::weak_ptr<CommonAPI::DBus::DBusProxyConnection::DBusSignalHandler>(self),
                                                                            false);
                }
            }
        }
    }
    return true;
}
