#ifndef Common_HPP
#define Common_HPP
#include <cstdint>
#include <limits>
#include <map>
#include <ostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace lib_srs
{
    constexpr int MAX_SERVICE_NAME_LENGTH = 256;
    constexpr int MAX_RECOVERY_ACTIONS    = 10;
    constexpr int MAX_RECOVERY_INTERVAL   = 3600; // in seconds

    enum class RecoveryState : uint8_t
    {
        RESTART = 0,
        STOP    = 1,
        DISABLE = 2,
        CRASHED = 3,
        UNKNOWN = std::numeric_limits<uint8_t>::max()
    };

    inline std::string toString(RecoveryState state)
    {
        std::string lStrRet = "";
        switch (state)
        {
        case RecoveryState::RESTART:
            lStrRet = "RESTART";
            break;
        case RecoveryState::STOP:
            lStrRet = "STOP";
            break;
        case RecoveryState::DISABLE:
            lStrRet = "DISABLE";
            break;
        case RecoveryState::CRASHED:
            lStrRet = "CRASHED";
            break;
        case RecoveryState::UNKNOWN:
            lStrRet = "UNKNOWN";
            break;
        }
        return lStrRet;
    }

    inline std::ostream &operator<<(std::ostream &os, RecoveryState state)
    {
        return os << toString(state);
    }
    enum class ServiceId : uint32_t
    {
        APP_A       = 2100,
        APP_B       = 2101,
        APP_C       = 2102,
        APP_D       = 2103,
        APP_E       = 2104,
        APP_F       = 2105,
        APP_G       = 2106,
        APP_H       = 2107,
        APP_I       = 2108,
        APP_J       = 2109,
        APP_K       = 2110,
        APP_L       = 2111,
        APP_M       = 2112,
        APP_N       = 2113,
        APP_O       = 2114,
        APP_P       = 2115,
        APP_Q       = 2116,
        APP_R       = 2117,
        APP_S       = 2118,
        APP_T       = 2119,
        APP_UNKNOWN = std::numeric_limits<uint32_t>::max()
    };
    inline std::map<ServiceId, std::string> g_MapServiceNames =
        {
            {ServiceId::APP_A, "AppA"},
            {ServiceId::APP_B, "AppB"},
            {ServiceId::APP_C, "AppC"},
            {ServiceId::APP_D, "AppD"},
            {ServiceId::APP_E, "AppE"},
            {ServiceId::APP_F, "AppF"},
            {ServiceId::APP_G, "AppG"},
            {ServiceId::APP_H, "AppH"},
            {ServiceId::APP_I, "AppI"},
            {ServiceId::APP_J, "AppJ"},
            {ServiceId::APP_K, "AppK"},
            {ServiceId::APP_L, "AppL"},
            {ServiceId::APP_M, "AppM"},
            {ServiceId::APP_N, "AppN"},
            {ServiceId::APP_O, "AppO"},
            {ServiceId::APP_P, "AppP"},
            {ServiceId::APP_Q, "AppQ"},
            {ServiceId::APP_R, "AppR"},
            {ServiceId::APP_S, "AppS"},
            {ServiceId::APP_T, "AppT"}};

} // namespace lib_srs

#endif