/**
 * @file CDelHelX_Logging.cpp
 * @brief Logging layer; writes messages and debug output to the EuroScope chat area.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CDelHelX_Logging.h"
#include "constants.h"

CDelHelX_Logging::CDelHelX_Logging()
{
    this->flashOnMessage = false;

    std::ostringstream msg;
    msg << "Version " << PLUGIN_VERSION << " loaded.";
    this->LogMessage(msg.str(), "Init");
}

/// @brief Displays a message in the EuroScope chat window.
/// @param message Text to display.
/// @param type Category label used as the sender name.
void CDelHelX_Logging::LogMessage(const std::string& message, const std::string& type)
{
    this->DisplayUserMessage(PLUGIN_NAME, type.c_str(), message.c_str(), true, true, true, this->flashOnMessage, false);
}

/// @brief Displays a message only when debug mode is active.
/// @param message Text to display.
/// @param type Category label used as the sender name.
void CDelHelX_Logging::LogDebugMessage(const std::string& message, const std::string& type)
{
    if (this->debug)
    {
        this->LogMessage(message, type);
    }
}