/**
 * @file  Logger.hpp
 * @brief Minimal DLT-styled logger used throughout libservice_recovery_scheduler.
 *
 * Line format (mirrors what dlt-viewer shows, but ANSI-colored on a TTY):
 * ```
 *   YYYY-MM-DD HH:MM:SS.uuuuuu [t<tid>] LVL SYM [APID] [CTID] {func:line} message
 * ```
 * `APID` / `CTID` follow the DLT convention of 4-char identifiers
 * (Application Id / Context Id). Use it like:
 * ```cpp
 *   LOG_INFO("SRSC", "CORE", "registered " << name << " pid=" << pid);
 * ```
 */
#ifndef LIB_SRS_LOGGER_HPP
#define LIB_SRS_LOGGER_HPP

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace lib_srs
{
    /// Severity ladder; ordered so `>=` comparisons against @ref Logger::threshold work.
    enum class LogLevel : uint8_t
    {
        TRACE = 0, ///< Verbose entry/exit tracing.
        DEBUG = 1, ///< Development-only diagnostics.
        INFO  = 2, ///< Normal operational progress.
        WARN  = 3, ///< Recoverable anomaly worth flagging.
        ERROR = 4, ///< Failure that prevented an operation from completing.
        FATAL = 5, ///< Unrecoverable condition; usually followed by shutdown.
    };

    /**
     * @brief Header-only sink used by the `LOG_*` macros.
     *
     * All state is in function-local statics so the class can safely be used
     * before / after `main()` and across shared-library boundaries.
     */
    class Logger
    {
    public:
        /**
         * @brief Runtime severity floor. Records with a lower level are
         *        dropped by the `LOG_*` macros without formatting them.
         * @return Mutable reference so callers can reassign, e.g.
         *         `Logger::threshold() = LogLevel::WARN;`.
         */
        static LogLevel &threshold()
        {
            static LogLevel t = LogLevel::TRACE;
            return t;
        }

        /**
         * @brief Whether ANSI color escapes should be emitted.
         *
         * Defaults to `true` when `stdout` is a TTY, `false` otherwise
         * (so log files stay clean). Mutable so tests can force either mode.
         */
        static bool &useColor()
        {
            static bool c = ::isatty(fileno(stdout)) != 0;
            return c;
        }

        /**
         * @brief Emit a single formatted log line to `stdout`.
         *
         * Called by the `LOG_*` macros; direct use is possible but rarely
         * needed.
         *
         * @param lvl   Severity of this record.
         * @param apid  4-char DLT Application Id (may be `nullptr`).
         * @param ctid  4-char DLT Context Id (may be `nullptr`).
         * @param func  Originating function name (usually `__func__`).
         * @param line  Originating source line (usually `__LINE__`).
         * @param msg   Pre-formatted message body.
         */
        static void write(LogLevel lvl, const char *apid, const char *ctid,
                          const char *func, int line, const std::string &msg)
        {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto tt  = system_clock::to_time_t(now);
            const auto us  = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;
            std::tm tmv{};
            ::localtime_r(&tt, &tmv);

            const char *lvlStr = "???";
            const char *sym    = " ";
            const char *color  = "";
            switch (lvl)
            {
            case LogLevel::TRACE:
                lvlStr = "TRC";
                sym    = "\u25AB";
                color  = "\x1b[90m";
                break; // ▫ dim gray
            case LogLevel::DEBUG:
                lvlStr = "DBG";
                sym    = "\u2699";
                color  = "\x1b[36m";
                break; // ⚙ cyan
            case LogLevel::INFO:
                lvlStr = "INF";
                sym    = "\u2139";
                color  = "\x1b[32m";
                break; // ℹ green
            case LogLevel::WARN:
                lvlStr = "WRN";
                sym    = "\u26A0";
                color  = "\x1b[33m";
                break; // ⚠ yellow
            case LogLevel::ERROR:
                lvlStr = "ERR";
                sym    = "\u2718";
                color  = "\x1b[31m";
                break; // ✘ red
            case LogLevel::FATAL:
                lvlStr = "FAT";
                sym    = "\u2620";
                color  = "\x1b[1;31m";
                break; // ☠ bold red
            }
            const char *reset = "\x1b[0m";
            const bool tty    = useColor();

            const long tid    = ::syscall(SYS_gettid);

            static std::mutex sMutex;
            std::lock_guard<std::mutex> lock(sMutex);
            std::cout << (tty ? color : "")
                      << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S")
                      << '.' << std::setw(6) << std::setfill('0') << us.count() << std::setfill(' ')
                      << " [t" << std::setw(6) << std::right << tid << ']'
                      << ' ' << lvlStr
                      << ' ' << sym
                      << " [" << std::left << std::setw(4) << (apid ? apid : "    ") << std::right << ']'
                      << " [" << std::left << std::setw(4) << (ctid ? ctid : "    ") << std::right << "]"
                      << " {" << (func ? func : "?") << ':' << line << "} "
                      << msg
                      << (tty ? reset : "")
                      << '\n';
            std::cout.flush();
        }
    };

} // namespace lib_srs

/**
 * @def SRS_LOG_
 * @brief Internal helper macro used by the public `LOG_*` macros.
 *
 * Short-circuits on the runtime threshold so `expr` is not evaluated when
 * the record would be dropped.
 */
#define SRS_LOG_(lvl, apid, ctid, expr)                                                          \
    do                                                                                           \
    {                                                                                            \
        if ((lvl) >= ::lib_srs::Logger::threshold())                                             \
        {                                                                                        \
            std::ostringstream _srs_oss;                                                         \
            _srs_oss << expr;                                                                    \
            ::lib_srs::Logger::write((lvl), (apid), (ctid), __func__, __LINE__, _srs_oss.str()); \
        }                                                                                        \
    } while (0)

/// @brief Log at `TRACE` severity. See @ref SRS_LOG_ for evaluation semantics.
#define LOG_TRACE(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::TRACE, apid, ctid, expr)
/// @brief Log at `DEBUG` severity.
#define LOG_DEBUG(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::DEBUG, apid, ctid, expr)
/// @brief Log at `INFO` severity.
#define LOG_INFO(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::INFO, apid, ctid, expr)
/// @brief Log at `WARN` severity.
#define LOG_WARN(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::WARN, apid, ctid, expr)
/// @brief Log at `ERROR` severity.
#define LOG_ERROR(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::ERROR, apid, ctid, expr)
/// @brief Log at `FATAL` severity.
#define LOG_FATAL(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::FATAL, apid, ctid, expr)

#endif // LIB_SRS_LOGGER_HPP
