#pragma once
#include "CDelHelX_Base.h"

/// @brief Plugin layer that provides chat-message logging to the EuroScope message area.
class CDelHelX_Logging : public CDelHelX_Base
{
public:
	/// @brief Constructs the logging layer and logs the plugin version on startup.
	CDelHelX_Logging();

protected:
	bool flashOnMessage; ///< When true, the EuroScope message area flashes on every logged message

	/// @brief Displays a message in the EuroScope chat window under the plugin name.
	/// @param message Text to display.
	/// @param type Category label shown as the sender (e.g. "Init", "Config").
	void LogMessage(const std::string& message, const std::string& type);

	/// @brief Displays a message only when debug mode is enabled.
	/// @param message Text to display.
	/// @param type Category label shown as the sender.
	void LogDebugMessage(const std::string& message, const std::string& type);
};
