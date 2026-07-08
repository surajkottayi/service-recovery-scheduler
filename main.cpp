#include "CRecoveryScheduler.hpp"
#include "CRecoverySchedulerStubImpl.hpp"
#include "Common.hpp"
#include "Logger.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>

using namespace lib_srs;

namespace
{
    void printSnapshot(const std::shared_ptr<CRecoveryScheduler> &scheduler, const std::string &name)
    {
        const auto lSnap = scheduler->getServiceState(name);
        if (lSnap.found)
        {
            LOG_INFO("SRSC", "SNAP", "name=" << name << " liveness=" << (lSnap.isOnline ? "ONLINE" : "CRASHED") << " last=" << toString(lSnap.lastAction) << " current=" << toString(lSnap.currentAction) << " next=" << toString(lSnap.nextAction) << " attempts=" << lSnap.attemptCount);
        }
        else
        {
            LOG_WARN("SRSC", "SNAP", "name=" << name << " not registered");
        }
    }

    void consoleLoop(std::shared_ptr<CRecoveryScheduler> scheduler)
    {
        LOG_INFO("SRSC", "CLI ", "console ready");
        while (true)
        {
            std::cerr << "\n\n\n\n\n\n\n\n\n\n";
            std::cerr << "\t\t\t\t\t\t|=====================================================|\n";
            std::cerr << "\t\t\t\t\t\t|--------------->SRS DEBUG/CONSOLE Mode<-------------|\n";
            std::cerr << "\t\t\t\t\t\t|=====================================================|\n";
            std::cerr << "\n\n\t\t\t\t\t\t\t\tEnter Input to continue\n";
            std::cerr << "\n\t\t\t\t\t\t\t\t 0. Exit\n";
            std::cerr << "\n\t\t\t\t\t\t\t\t 1. Query App State\n";
            std::cerr << "\n\t\t\t\t\t\t\t\t> ";

            int input = -1;
            if (!(std::cin >> input))
            {
                // EOF or non-numeric input — clear the fail bit and drain the
                // rest of the line so we don't spin.
                if (std::cin.eof())
                {
                    LOG_INFO("SRSC", "CLI ", "stdin closed, exiting console");
                    break;
                }
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            // Consume the trailing '\n' left by operator>>; otherwise the next
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (input == 0)
            {
                LOG_INFO("SRSC", "CLI ", "Exit requested");
                break;
            }
            if (input == 1)
            {
                std::cerr << "Enter AppName (blank = all), Eg: 'AppA'\n> ";
                std::string lName;
                if (!std::getline(std::cin, lName))
                {
                    continue;
                }
                if (!lName.empty())
                {
                    printSnapshot(scheduler, lName);
                }
                else
                {
                    for (const auto &kv : g_MapServiceNames)
                    {
                        printSnapshot(scheduler, kv.second);
                    }
                }
                continue;
            }
            LOG_WARN("SRSC", "CLI ", "unknown option: " << input);
        }
    }
} // namespace

int main(int argc, char **argv)
{
    bool lbConsoleMode = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "mode=console")
        {
            lbConsoleMode = true;
        }
    }

    auto lpRecoveryScheduler = CRecoveryScheduler::getInstance();
    lpRecoveryScheduler->init();
    if (!lpRecoveryScheduler->run())
    {
        LOG_ERROR("SRSC", "MAIN", "scheduler failed to bring up CommonAPI service - aborting");
    }
    else
    {

        if (lbConsoleMode)
        {
            std::thread lThread(consoleLoop, lpRecoveryScheduler);
            lThread.join();
        }
        else
        {
            LOG_INFO("SRSC", "MAIN", "running headless (pass 'mode=console' for interactive query)");
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    return 0;
}