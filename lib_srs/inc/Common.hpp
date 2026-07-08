/**
 * @file  Common.hpp
 * @brief Shared value types, enums and lookup tables used across the
 *        service-recovery-scheduler library and its clients.
 *
 * The definitions here are intentionally header-only so both the daemon
 * (@ref lib_srs::CRecoveryScheduler) and dummy clients under
 * `other_apps_dummy/` can share the exact same @ref lib_srs::RecoveryState
 * / @ref lib_srs::ServiceId vocabulary without pulling any transitive
 * dependency on the CommonAPI generated headers.
 */
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

/// Public namespace for every symbol exported by libservice_recovery_scheduler.
namespace lib_srs
{
    /// Upper bound for the fully-qualified D-Bus / CommonAPI service name.
    constexpr int MAX_SERVICE_NAME_LENGTH = 256;
    /// Maximum number of recovery actions a client may register per service.
    constexpr int MAX_RECOVERY_ACTIONS = 10;
    /// Maximum inter-attempt back-off window a client may request, in seconds.
    constexpr int MAX_RECOVERY_INTERVAL = 3600; // in seconds

    /**
     * @brief Lifecycle / recovery outcome for a monitored service.
     *
     * Values are stable u8 identifiers because they cross the D-Bus wire in
     * `reportServiceState` and are persisted in @ref SServiceRecoveryInfo.
     */
    enum class RecoveryState : uint8_t
    {
        RESTART = 0,                                  ///< Ask the peer to restart itself.
        STOP    = 1,                                  ///< Ask the peer to stop cleanly.
        DISABLE = 2,                                  ///< Take the peer offline and do not attempt recovery again.
        CRASHED = 3,                                  ///< Peer disappeared unexpectedly (detected via `NameOwnerChanged`).
        UNKNOWN = std::numeric_limits<uint8_t>::max() ///< Sentinel for uninitialised state.
    };

    /**
     * @brief Human-readable name for a @ref RecoveryState value.
     * @param state Enumerator to render.
     * @return Upper-case identifier matching the enumerator name.
     */
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

    /**
     * @brief Stream inserter so @ref RecoveryState works with `LOG_*` macros
     *        and `std::cout`.
     */
    inline std::ostream &operator<<(std::ostream &os, RecoveryState state)
    {
        return os << toString(state);
    }

    /**
     * @brief Fixed catalogue of well-known application identifiers.
     *
     * The numeric values are what a client would embed in a bus/instance name.
     * @ref g_MapServiceNames provides the reverse lookup used by logging /
     * introspection code.
     */
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
        APP_UNKNOWN = std::numeric_limits<uint32_t>::max() ///< Sentinel for unmapped ids.
    };

    /**
     * @brief Static id-to-name lookup for @ref ServiceId.
     *
     * @note `inline` at namespace scope gives every translation unit the same
     * definition (C++17) so this can safely live in the header.
     */
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