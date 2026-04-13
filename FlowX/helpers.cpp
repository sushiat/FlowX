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

void FillRectAlpha(HDC hDC, const RECT& rect, COLORREF color, int opacityPct)
{
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;
    if (opacityPct >= 100)
    {
        auto brush = CreateSolidBrush(color);
        FillRect(hDC, &rect, brush);
        DeleteObject(brush);
        return;
    }
    HDC     memDC  = CreateCompatibleDC(hDC);
    HBITMAP bmp    = CreateCompatibleBitmap(hDC, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);
    RECT    local  = {0, 0, w, h};
    auto    brush  = CreateSolidBrush(color);
    FillRect(memDC, &local, brush);
    DeleteObject(brush);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, static_cast<BYTE>(opacityPct * 255 / 100), 0};
    AlphaBlend(hDC, rect.left, rect.top, w, h, memDC, 0, 0, w, h, bf);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

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
    std::map<std::string, std::string> result;

    json j;
    try
    {
        j = json::parse(FetchVatsimData());
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("FetchAtisData", e.what());
        return result; // network error or malformed response — return empty, caller retries next cycle
    }

    if (!j.contains("atis") || !j["atis"].is_array())
        return result;

    for (auto& entry : j["atis"])
    {
        if (!entry.contains("callsign") || !entry["callsign"].is_string()) { continue; }
        if (!entry.contains("atis_code") || !entry["atis_code"].is_string()) { continue; }
        std::string cs   = entry["callsign"].get<std::string>();
        to_upper(cs);
        std::string code = entry["atis_code"].get<std::string>();
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