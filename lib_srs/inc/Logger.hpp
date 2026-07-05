#ifndef LIB_SRS_LOGGER_HPP
#define LIB_SRS_LOGGER_HPP

// Minimal DLT-styled logger.
//
// Line format (mirrors what dlt-viewer shows, but ANSI-colored on a TTY):
//   YYYY-MM-DD HH:MM:SS.uuuuuu [t<tid>] LVL SYM [APID] [CTID] {func:line} message
//
// APID / CTID follow the DLT convention of 4-char identifiers (Application
// Id / Context Id). Use it like:
//   LOG_INFO ("SRSC", "CORE", "registered " << name << " pid=" << pid);

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
    enum class LogLevel : uint8_t
    {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5,
    };

    class Logger
    {
    public:
        // Compile-time floor: anything below is short-circuited by the macros.
        static LogLevel &threshold()
        {
            static LogLevel t = LogLevel::TRACE;
            return t;
        }

        // Toggled off automatically when stdout is not a TTY.
        static bool &useColor()
        {
            static bool c = ::isatty(fileno(stdout)) != 0;
            return c;
        }

        static void write(LogLevel lvl, const char *apid, const char *ctid,
                          const char *func, int line, const std::string &msg)
        {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto tt = system_clock::to_time_t(now);
            const auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;
            std::tm tmv{};
            ::localtime_r(&tt, &tmv);

            const char *lvlStr = "???";
            const char *sym = " ";
            const char *color = "";
            switch (lvl)
            {
            case LogLevel::TRACE:
                lvlStr = "TRC";
                sym = "\u25AB";
                color = "\x1b[90m";
                break; // ▫ dim gray
            case LogLevel::DEBUG:
                lvlStr = "DBG";
                sym = "\u2699";
                color = "\x1b[36m";
                break; // ⚙ cyan
            case LogLevel::INFO:
                lvlStr = "INF";
                sym = "\u2139";
                color = "\x1b[32m";
                break; // ℹ green
            case LogLevel::WARN:
                lvlStr = "WRN";
                sym = "\u26A0";
                color = "\x1b[33m";
                break; // ⚠ yellow
            case LogLevel::ERROR:
                lvlStr = "ERR";
                sym = "\u2718";
                color = "\x1b[31m";
                break; // ✘ red
            case LogLevel::FATAL:
                lvlStr = "FAT";
                sym = "\u2620";
                color = "\x1b[1;31m";
                break; // ☠ bold red
            }
            const char *reset = "\x1b[0m";
            const bool tty = useColor();

            const long tid = ::syscall(SYS_gettid);

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

#define LOG_TRACE(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::TRACE, apid, ctid, expr)
#define LOG_DEBUG(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::DEBUG, apid, ctid, expr)
#define LOG_INFO(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::INFO, apid, ctid, expr)
#define LOG_WARN(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::WARN, apid, ctid, expr)
#define LOG_ERROR(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::ERROR, apid, ctid, expr)
#define LOG_FATAL(apid, ctid, expr) SRS_LOG_(::lib_srs::LogLevel::FATAL, apid, ctid, expr)

#endif // LIB_SRS_LOGGER_HPP
