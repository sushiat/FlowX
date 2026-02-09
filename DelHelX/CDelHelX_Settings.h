#pragma once
#include "CDelHelX_Logging.h"
#include "config.h"
#include "nlohmann/json.hpp"
#include <future>

using json = nlohmann::json;

class CDelHelX_Settings : public CDelHelX_Logging
{
public:
	CDelHelX_Settings();

protected:
	bool updateCheck;
	std::future<std::string> latestVersion;
	std::map<std::string, airport> airports;

	void LoadSettings();
	void SaveSettings();
	void LoadConfig();
	void CheckForUpdate();
};