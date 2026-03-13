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

void CDelHelX_Logging::LogMessage(const std::string& message, const std::string& type)
{
	this->DisplayUserMessage(PLUGIN_NAME, type.c_str(), message.c_str(), true, true, true, this->flashOnMessage, false);
}

void CDelHelX_Logging::LogDebugMessage(const std::string& message, const std::string& type)
{
	if (this->debug) {
		this->LogMessage(message, type);
	}
}