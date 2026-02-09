#pragma once
#include "CDelHelX_Base.h"

class CDelHelX_Logging : public CDelHelX_Base
{
public:
	CDelHelX_Logging();
protected:
	bool flashOnMessage;
	
	void LogMessage(const std::string& message, const std::string& type);
	void LogDebugMessage(const std::string& message, const std::string& type);
};
