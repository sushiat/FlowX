/**
 * @file CFlowX_Logging.h
 * @brief Declaration of CFlowX_Logging, the chat message logging layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_Base.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// @brief Plugin layer that provides chat-message logging to the EuroScope message area.
class CFlowX_Logging : public CFlowX_Base
{
  private:
    std::shared_ptr<std::mutex>    debugLogMutex   = std::make_shared<std::mutex>(); ///< Serialises concurrent async debug-log file writes.
    std::vector<std::future<void>> debugLogFutures;                                  ///< Pending async debug-log file writes; pruned on each LogDebugMessage call.

  protected:
    bool flashOnMessage; ///< When true, the EuroScope message area flashes on every logged message

    /// @brief Writes a timestamped session-start header to the debug log file.
    /// Should be called exactly when debug mode turns on (startup with debug=true, or manual toggle).
    void LogDebugSessionStart();

    /// @brief Writes a message to the debug log file only (no EuroScope chat) when debug mode is enabled.
    /// @param message Text to write.
    /// @param type Category label shown in the file.
    void LogDebugFileOnly(const std::string& message, const std::string& type);

    /// @brief Logs a caught exception to debugLog.txt unconditionally (regardless of debug mode) and
    ///        displays an alert in the EuroScope chat window.
    /// @param context Name of the function or handler where the exception was caught.
    /// @param what    Exception message text.
    void LogException(const std::string& context, const std::string& what);

    /// @brief Displays a message in the EuroScope chat window under the plugin name.
    /// @param message Text to display.
    /// @param type Category label shown as the sender (e.g. "Init", "Config").
    void LogMessage(const std::string& message, const std::string& type);

  public:
    /// @brief Displays a message only when debug mode is enabled.
    /// @param message Text to display.
    /// @param type Category label shown as the sender.
    void LogDebugMessage(const std::string& message, const std::string& type);
    /// @brief Constructs the logging layer and logs the plugin version on startup.
    CFlowX_Logging();
};
