/**
 * @file CFlowX_Logging.cpp
 * @brief Logging layer; writes messages and debug output to the EuroScope chat area.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX_Logging.h"
#include "constants.h"
#include "helpers.h"

CFlowX_Logging::CFlowX_Logging()
{
    this->flashOnMessage = false;

    std::ostringstream msg;
    msg << "Version " << PLUGIN_VERSION << " loaded.";
    this->LogMessage(msg.str(), "Init");
}

/// @brief Displays a message in the EuroScope chat window.
/// @param message Text to display.
/// @param type Category label used as the sender name.
void CFlowX_Logging::LogMessage(const std::string& message, const std::string& type)
{
    this->DisplayUserMessage(PLUGIN_NAME, type.c_str(), message.c_str(), true, true, true, this->flashOnMessage, false);
}

/// @brief Displays a message only when debug mode is active.
/// @param message Text to display.
/// @param type Category label used as the sender name.
void CFlowX_Logging::LogDebugMessage(const std::string& message, const std::string& type)
{
    if (!this->debug)
        return;

    this->LogMessage(message, type);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[20];
    sprintf_s(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string entry     = std::string(ts) + " [" + type + "] " + message;
    std::filesystem::path logPath = std::filesystem::path(GetPluginDirectory()) / "debugLog.txt";

    // Prune completed futures, then launch new write
    this->debugLogFutures.erase(
        std::remove_if(this->debugLogFutures.begin(), this->debugLogFutures.end(),
            [](const std::future<void>& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
        this->debugLogFutures.end());

    this->debugLogFutures.push_back(std::async(std::launch::async,
        [entry, logPath]()
        {
            std::ofstream f(logPath, std::ios::app);
            if (f) f << entry << "\n";
        }));
}