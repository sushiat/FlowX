/**
 * @file osm_taxiways.cpp
 * @brief Fetches and caches LOWW taxiway/taxilane geometry from OpenStreetMap via the Overpass API.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "osm_taxiways.h"
#include "helpers.h"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <WinInet.h>

using json = nlohmann::json;

static constexpr const char* OVERPASS_HOST  = "overpass-api.de";
static constexpr const char* OVERPASS_PATH  = "/api/interpreter";
static constexpr const char* CACHE_FILENAME = "osm_taxiways_LOWW.json";

static constexpr const char* OVERPASS_QUERY =
    "[out:json][timeout:25];\n"
    "(\n"
    "  way[\"aeroway\"=\"taxiway\"](around:6500,48.1103,16.5697);\n"
    "  way[\"aeroway\"=\"taxilane\"](around:6500,48.1103,16.5697);\n"
    ");\n"
    "out geom;";

/// @brief URL-encodes a string for use in application/x-www-form-urlencoded POST bodies.
static std::string UrlEncode(std::string_view s)
{
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

/// @brief Parses a JSON string in either Overpass (elements[]) or cache (ways[]) format.
static OsmResult ParseOsmJson(const std::string& raw)
{
    try
    {
        const auto j = json::parse(raw);
        OsmAirportData data;

        const json* arr = nullptr;
        if      (j.contains("ways"))     arr = &j.at("ways");
        else if (j.contains("elements")) arr = &j.at("elements");
        else return std::unexpected(std::string("JSON missing 'ways'/'elements' key"));

        for (const auto& el : *arr)
        {
            OsmWay way;
            way.id = el.contains("id") ? el.at("id").get<int64_t>() : int64_t{0};

            if (el.contains("tags"))
            {
                // Overpass format — aeroway type lives inside the tags object
                const auto& tags = el.at("tags");
                const std::string aeroway = tags.value("aeroway", std::string{});
                if      (aeroway == "taxiway")  way.type = AerowayType::Taxiway;
                else if (aeroway == "taxilane") way.type = AerowayType::Taxilane;
                else                            way.type = AerowayType::Unknown;
                way.ref  = tags.value("ref",  std::string{});
                way.name = tags.value("name", std::string{});
            }
            else
            {
                // Cache format — type/ref/name are top-level fields
                const std::string typeStr = el.value("type", std::string{});
                if      (typeStr == "taxiway")  way.type = AerowayType::Taxiway;
                else if (typeStr == "taxilane") way.type = AerowayType::Taxilane;
                else                            way.type = AerowayType::Unknown;
                way.ref  = el.value("ref",  std::string{});
                way.name = el.value("name", std::string{});
            }

            if (el.contains("geometry"))
            {
                for (const auto& gp : el.at("geometry"))
                    way.geometry.push_back({ gp.value("lat", 0.0), gp.value("lon", 0.0) });
            }

            if (!way.geometry.empty())
                data.ways.push_back(std::move(way));
        }
        return data;
    }
    catch (const std::exception& ex)
    {
        return std::unexpected(std::string("JSON parse error: ") + ex.what());
    }
}

/// @brief Serialises OsmAirportData to the cache JSON file in the plugin directory.
static void SaveCache(const OsmAirportData& data)
{
    try
    {
        json j;
        auto& waysArr = j["ways"] = json::array();
        for (const auto& w : data.ways)
        {
            json wj;
            wj["id"]   = w.id;
            wj["type"] = (w.type == AerowayType::Taxiway)  ? "taxiway"
                       : (w.type == AerowayType::Taxilane) ? "taxilane" : "unknown";
            wj["ref"]  = w.ref;
            wj["name"] = w.name;
            auto& geom = wj["geometry"] = json::array();
            for (const auto& gp : w.geometry)
                geom.push_back({ {"lat", gp.lat}, {"lon", gp.lon} });
            waysArr.push_back(std::move(wj));
        }

        const std::filesystem::path path = std::filesystem::path(GetPluginDirectory()) / CACHE_FILENAME;
        std::ofstream f(path);
        if (f) f << j.dump(2);
    }
    catch (...) {} // best-effort; the fetch result is still returned to the caller
}

OsmResult fetchLOWWTaxiways()
{
    std::ostringstream agent;
    agent << PLUGIN_NAME << '/' << PLUGIN_VERSION;

    HINTERNET hNet = InternetOpen(agent.str().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet)
        return std::unexpected(std::format("OSM fetch: InternetOpen failed ({})", GetLastError()));

    HINTERNET hConn = InternetConnect(hNet, OVERPASS_HOST, INTERNET_DEFAULT_HTTPS_PORT,
                                      NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn)
    {
        InternetCloseHandle(hNet);
        return std::unexpected(std::format("OSM fetch: InternetConnect failed ({})", GetLastError()));
    }

    const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hReq = HttpOpenRequest(hConn, "POST", OVERPASS_PATH, NULL, NULL, NULL, flags, 0);
    if (!hReq)
    {
        InternetCloseHandle(hConn);
        InternetCloseHandle(hNet);
        return std::unexpected(std::format("OSM fetch: HttpOpenRequest failed ({})", GetLastError()));
    }

    const std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    const std::string body    = "data=" + UrlEncode(OVERPASS_QUERY);

    const BOOL sent = HttpSendRequest(hReq,
                                      headers.c_str(), static_cast<DWORD>(headers.size()),
                                      const_cast<char*>(body.c_str()), static_cast<DWORD>(body.size()));
    if (!sent)
    {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hNet);
        return std::unexpected(std::format("OSM fetch: HttpSendRequest failed ({})", GetLastError()));
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    HttpQueryInfo(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);
    if (statusCode != 200)
    {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hNet);
        return std::unexpected(std::format("OSM fetch: HTTP status {}", statusCode));
    }

    char   buf[4096];
    DWORD  bytesRead;
    std::string response;
    while (InternetReadFile(hReq, buf, sizeof(buf), &bytesRead) && bytesRead)
        response.append(buf, bytesRead);

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hNet);

    auto result = ParseOsmJson(response);
    if (result.has_value())
        SaveCache(result.value());
    return result;
}

OsmResult loadCachedTaxiways()
{
    const std::filesystem::path path = std::filesystem::path(GetPluginDirectory()) / CACHE_FILENAME;
    if (!std::filesystem::exists(path))
        return std::unexpected(std::string("OSM cache: no cache file found"));

    std::ifstream f(path);
    if (!f)
        return std::unexpected(std::string("OSM cache: failed to open cache file"));

    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseOsmJson(ss.str());
}
