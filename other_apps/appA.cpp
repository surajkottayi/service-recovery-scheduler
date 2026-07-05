// AppA — sample CommonAPI D-Bus client for RecoveryScheduler.
//
// Enrolls itself with the scheduler and subscribes to state-change broadcasts.
// Runtime routing is picked from $COMMONAPI_CONFIG (see fidl/commonapi4dbus.ini).

#include <CommonAPI/CommonAPI.hpp>
#include <v1/com/bmw/recovery/RecoverySchedulerProxy.hpp>

#include <chrono>
#include <iostream>
#include <thread>
#include "Common.hpp"
#include "Logger.hpp"
using namespace lib_srs;

namespace srs = ::v1::com::bmw::recovery;

int main()
{
    auto runtime = CommonAPI::Runtime::get();
    std::string lAppName = g_MapServiceNames[ServiceId::APP_A];
    auto proxy = runtime->buildProxy<srs::RecoverySchedulerProxy>("local", "com.bmw.recovery.RecoveryScheduler", lAppName);

    if (proxy)
    {

        LOG_INFO("APPA", "MAIN", "waiting for scheduler to be available...");
        while (!proxy->isAvailable())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        proxy->getServiceStateChangedEvent().subscribe([lAppName](const std::string &name, srs::RecoveryScheduler::RecoveryState st)
                                                       { LOG_INFO("APPA", "EVT ", "state changed name=" << name
                                                                                                        << " action=" << static_cast<int>(st)); });

        CommonAPI::CallStatus status{};
        srs::RecoveryScheduler::RegisterResult result{};
        proxy->registerService(lAppName,
                               {srs::RecoveryScheduler::RecoveryState::RESTART,
                                srs::RecoveryScheduler::RecoveryState::RESTART,
                                srs::RecoveryScheduler::RecoveryState::RESTART,
                                srs::RecoveryScheduler::RecoveryState::STOP,
                                srs::RecoveryScheduler::RecoveryState::DISABLE},
                               -1,
                               status,
                               result);

        LOG_INFO("APPA", "MAIN", "register status=" << static_cast<int>(status)
                                                    << " result=" << static_cast<int>(result));

        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}