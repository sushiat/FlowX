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
#include <string>
#include <vector>

/// @brief Plugin layer that provides chat-message logging to the EuroScope message area.
class CFlowX_Logging : public CFlowX_Base
{
  private:
    std::vector<std::future<void>> debugLogFutures; ///< Pending async debug-log file writes; pruned on each LogDebugMessage call.

  protected:
    bool flashOnMessage; ///< When true, the EuroScope message area flashes on every logged message

    /// @brief Displays a message only when debug mode is enabled.
    /// @param message Text to display.
    /// @param type Category label shown as the sender.
    void LogDebugMessage(const std::string& message, const std::string& type);

    /// @brief Displays a message in the EuroScope chat window under the plugin name.
    /// @param message Text to display.
    /// @param type Category label shown as the sender (e.g. "Init", "Config").
    void LogMessage(const std::string& message, const std::string& type);

  public:
    /// @brief Constructs the logging layer and logs the plugin version on startup.
    CFlowX_Logging();
};
