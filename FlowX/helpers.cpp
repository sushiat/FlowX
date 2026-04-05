/**
 * @file helpers.cpp
 * @brief WinInet HTTP helpers; implements FetchLatestVersion and FetchVatsimData.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"

#include "helpers.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/// @brief Fetches the latest plugin version string from the remote version file.
/// @return Version string read from the URL (e.g. "0.6.0").
/// @note Throws error on connection or HTTP failure.
std::string FetchVatsimData()
{
    std::ostringstream agent;
    agent << PLUGIN_NAME << '/' << PLUGIN_VERSION;

    HINTERNET init = InternetOpen(agent.str().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!init) {
        throw error{ std::format("VATSIM data: connection failed. Error: {}", GetLastError()) };
    }

    HINTERNET open = InternetOpenUrl(init, VATSIM_DATA_URL, NULL, 0, INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD, 0);
    if (!open) {
        InternetCloseHandle(init);
        throw error{ std::format("VATSIM data: failed to open URL. Error: {}", GetLastError()) };
    }

    char data[4096];
    DWORD read;
    std::string result;

    while (InternetReadFile(open, data, sizeof(data), &read) && read) {
        result.append(data, read);
    }

    InternetCloseHandle(open);
    InternetCloseHandle(init);

    return result;
}

std::map<std::string, std::string> FetchAtisData(std::vector<std::string> airports)
{
    auto j = json::parse(FetchVatsimData());

    std::map<std::string, std::string> result;

    for (auto& entry : j.at("atis"))
    {
        std::string cs = entry.value("callsign", "");
        to_upper(cs);
        std::string code = entry.value("atis_code", "");
        if (code.empty())
        {
            continue;
        }

        for (const auto& icao : airports)
        {
            if (!cs.contains(icao))
            {
                continue;
            }

            // First match wins; a _D_ callsign overrides any earlier non-_D_ match
            auto it = result.find(icao);
            if (it == result.end() || cs.contains("_D_"))
            {
                result[icao] = code;
            }

            break; // each ATIS station matches at most one airport
        }
    }

    return result;
}

std::string FetchLatestVersion()
{
    std::ostringstream agent;
    agent << PLUGIN_NAME << '/' << PLUGIN_VERSION;

    HINTERNET init = InternetOpen(agent.str().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!init) {
        throw error{ std::format("Connection failed. Error: {}", GetLastError()) };
    }

    HINTERNET open = InternetOpenUrl(init, PLUGIN_LATEST_VERSION_URL, NULL, 0, INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD, 0);
    if (!open) {
        InternetCloseHandle(init);
        throw error{ std::format("Failed to load URL. Error: {}", GetLastError()) };
    }

    char data[256];
    DWORD read;
    std::string version;

    while (InternetReadFile(open, data, 256, &read) && read) {
        version.append(data, read);
    }

    InternetCloseHandle(init);
    InternetCloseHandle(open);

    return version;
}