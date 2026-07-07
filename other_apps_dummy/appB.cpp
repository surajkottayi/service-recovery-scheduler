// AppB — sample CommonAPI D-Bus client for RecoveryScheduler.
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
    std::string lAppName = g_MapServiceNames[ServiceId::APP_B];
    auto proxy = runtime->buildProxy<srs::RecoverySchedulerProxy>("local", "com.bmw.recovery.RecoveryScheduler", lAppName);

    if (proxy)
    {

        LOG_INFO("APPB", "MAIN", "waiting for scheduler to be available...");
        while (!proxy->isAvailable())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        proxy->getServiceStateChangedEvent().subscribe([lAppName](const std::string &name, srs::RecoveryScheduler::RecoveryState st)
                                                       { LOG_INFO("APPB", "EVT ", "state changed name=" << name << " action=" << static_cast<int>(st)); });

        CommonAPI::CallStatus status{};
        srs::RecoveryScheduler::RegisterResult result{};
        proxy->registerService(lAppName,
                               {srs::RecoveryScheduler::RecoveryState::STOP,
                                srs::RecoveryScheduler::RecoveryState::DISABLE},
                               -1,
                               status,
                               result);

        LOG_INFO("APPB", "MAIN", "register status=" << static_cast<int>(status) << " result=" << static_cast<int>(result));

        auto pushState = [&](srs::RecoveryScheduler::RecoveryState current,
                             srs::RecoveryScheduler::RecoveryState last)
        {
            CommonAPI::CallStatus lPushStatus{};
            srs::RecoveryScheduler::QueryResult lPushResult{};
            proxy->reportServiceState(lAppName, current, last, lPushStatus, lPushResult);
            LOG_INFO("APPB", "PUSH", "reportServiceState current=" << static_cast<int>(current) << " last=" << static_cast<int>(last) << " status=" << static_cast<int>(lPushStatus) << " result=" << static_cast<int>(lPushResult));
        };

        pushState(srs::RecoveryScheduler::RecoveryState::STOP,
                  srs::RecoveryScheduler::RecoveryState::UNKNOWN);

        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return 0;
}