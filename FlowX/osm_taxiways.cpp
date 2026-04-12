/**
 * @file osm_taxiways.cpp
 * @brief Fetches and caches taxiway/taxilane geometry from OpenStreetMap via the Overpass API.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "osm_taxiways.h"
#include "helpers.h"
#include "nlohmann/json.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <WinInet.h>

using json = nlohmann::json;

static constexpr const char* OVERPASS_HOST = "overpass-api.de";
static constexpr const char* OVERPASS_PATH = "/api/interpreter";

/// @brief Returns the cache filename for a given airport ICAO code.
static std::string CacheFilename(const std::string& icao)
{
    return "osm_taxiways_" + icao + ".json";
}

/// @brief Builds an Overpass QL query for taxiway/taxilane/runway/holding-position data around a centre point.
static std::string BuildOverpassQuery(double centerLat, double centerLon, int radiusM)
{
    return std::format(
        "[out:json][timeout:25];\n"
        "(\n"
        "  way[\"aeroway\"=\"taxiway\"](around:{},{:.4f},{:.4f});\n"
        "  way[\"aeroway\"=\"taxilane\"](around:{},{:.4f},{:.4f});\n"
        "  way[\"aeroway\"=\"runway\"](around:{},{:.4f},{:.4f});\n"
        "  node[\"aeroway\"=\"holding_position\"](around:{},{:.4f},{:.4f});\n"
        ");\n"
        "out geom;",
        radiusM, centerLat, centerLon,
        radiusM, centerLat, centerLon,
        radiusM, centerLat, centerLon,
        radiusM, centerLat, centerLon);
}

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
        const auto     j = json::parse(raw);
        OsmAirportData data;

        if (j.contains("elements"))
        {
            // Overpass format — ways and nodes arrive together in the elements array.
            for (const auto& el : j.at("elements"))
            {
                const std::string elType = el.value("type", std::string{});

                if (elType == "node")
                {
                    OsmHoldingPosition hp;
                    hp.id  = el.contains("id") ? el.at("id").get<int64_t>() : int64_t{0};
                    hp.pos = {el.value("lat", 0.0), el.value("lon", 0.0)};
                    if (el.contains("tags"))
                    {
                        const auto& tags = el.at("tags");
                        hp.ref           = tags.value("ref", std::string{});
                        hp.name          = tags.value("name", std::string{});
                    }
                    data.holdingPositions.push_back(std::move(hp));
                }
                else // "way" or untyped
                {
                    OsmWay way;
                    way.id = el.contains("id") ? el.at("id").get<int64_t>() : int64_t{0};
                    if (el.contains("tags"))
                    {
                        const auto&       tags    = el.at("tags");
                        const std::string aeroway = tags.value("aeroway", std::string{});
                        if (aeroway == "taxiway")
                            way.type = AerowayType::Taxiway;
                        else if (aeroway == "taxilane")
                            way.type = AerowayType::Taxilane;
                        else if (aeroway == "runway")
                            way.type = AerowayType::Runway;
                        else
                            way.type = AerowayType::Unknown;
                        way.ref  = tags.value("ref", std::string{});
                        way.name = tags.value("name", std::string{});
                    }
                    if (el.contains("geometry"))
                    {
                        for (const auto& gp : el.at("geometry"))
                            way.geometry.push_back({gp.value("lat", 0.0), gp.value("lon", 0.0)});
                    }
                    if (!way.geometry.empty())
                        data.ways.push_back(std::move(way));
                }
            }
        }
        else if (j.contains("ways"))
        {
            // Cache format — ways and holding positions are stored in separate arrays.
            for (const auto& el : j.at("ways"))
            {
                OsmWay way;
                way.id                    = el.contains("id") ? el.at("id").get<int64_t>() : int64_t{0};
                const std::string typeStr = el.value("type", std::string{});
                if (typeStr == "taxiway")
                    way.type = AerowayType::Taxiway;
                else if (typeStr == "taxilane")
                    way.type = AerowayType::Taxilane;
                else if (typeStr == "taxiway_holdingpoint")
                    way.type = AerowayType::Taxiway_HoldingPoint;
                else if (typeStr == "taxiway_intersection")
                    way.type = AerowayType::Taxiway_Intersection;
                else if (typeStr == "runway")
                    way.type = AerowayType::Runway;
                else
                    way.type = AerowayType::Unknown;
                way.ref  = el.value("ref", std::string{});
                way.name = el.value("name", std::string{});
                if (el.contains("geometry"))
                {
                    for (const auto& gp : el.at("geometry"))
                        way.geometry.push_back({gp.value("lat", 0.0), gp.value("lon", 0.0)});
                }
                if (!way.geometry.empty())
                    data.ways.push_back(std::move(way));
            }

            if (j.contains("holdingPositions"))
            {
                for (const auto& el : j.at("holdingPositions"))
                {
                    OsmHoldingPosition hp;
                    hp.id   = el.contains("id") ? el.at("id").get<int64_t>() : int64_t{0};
                    hp.ref  = el.value("ref", std::string{});
                    hp.name = el.value("name", std::string{});
                    hp.pos  = {el.value("lat", 0.0), el.value("lon", 0.0)};
                    data.holdingPositions.push_back(std::move(hp));
                }
            }
        }
        else
        {
            return std::unexpected(std::string("JSON missing 'ways'/'elements' key"));
        }
        data.preAnnotated = j.value("annotated", false);
        return data;
    }
    catch (const std::exception& ex)
    {
        return std::unexpected(std::string("JSON parse error: ") + ex.what());
    }
}

void SaveOsmCache(const std::string& icao, const OsmAirportData& data)
{
    try
    {
        json j;
        j["annotated"] = true;
        auto& waysArr = j["ways"] = json::array();
        for (const auto& w : data.ways)
        {
            json wj;
            wj["id"]   = w.id;
            wj["type"] = (w.type == AerowayType::Taxiway_HoldingPoint)   ? "taxiway_holdingpoint"
                         : (w.type == AerowayType::Taxiway_Intersection) ? "taxiway_intersection"
                         : (w.type == AerowayType::Taxiway)              ? "taxiway"
                         : (w.type == AerowayType::Taxilane)             ? "taxilane"
                         : (w.type == AerowayType::Runway)               ? "runway"
                                                                         : "unknown";
            wj["ref"]  = w.ref;
            wj["name"] = w.name;
            auto& geom = wj["geometry"] = json::array();
            for (const auto& gp : w.geometry)
                geom.push_back({{"lat", gp.lat}, {"lon", gp.lon}});
            waysArr.push_back(std::move(wj));
        }

        auto& hpArr = j["holdingPositions"] = json::array();
        for (const auto& hp : data.holdingPositions)
        {
            json hj;
            hj["id"]   = hp.id;
            hj["ref"]  = hp.ref;
            hj["name"] = hp.name;
            hj["lat"]  = hp.pos.lat;
            hj["lon"]  = hp.pos.lon;
            hpArr.push_back(std::move(hj));
        }

        const std::filesystem::path path = std::filesystem::path(GetPluginDirectory()) / CacheFilename(icao);
        std::ofstream               f(path);
        if (f)
            f << j.dump(2);
    }
    catch (...)
    {
    } // best-effort
}

void DeleteOsmCache(const std::string& icao)
{
    try
    {
        const std::filesystem::path path = std::filesystem::path(GetPluginDirectory()) / CacheFilename(icao);
        std::filesystem::remove(path);
    }
    catch (...)
    {
    } // best-effort
}

double WayLengthM(const OsmWay& way)
{
    double total = 0.0;
    for (size_t k = 1; k < way.geometry.size(); ++k)
    {
        const auto&  a    = way.geometry[k - 1];
        const auto&  b    = way.geometry[k];
        const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
        const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
        const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
        const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
        const double h    = std::sin(dLat / 2) * std::sin(dLat / 2) + cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
        total += 6'371'000.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
    }
    return total;
}

OsmResult fetchTaxiways(const std::string& icao, double centerLat, double centerLon, int radiusM)
{
    std::ostringstream agent;
    agent << PLUGIN_NAME << '/' << PLUGIN_VERSION;

    HINTERNET hNet = InternetOpen(agent.str().c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet)
        return std::unexpected(std::format("OSM fetch {}: InternetOpen failed ({})", icao, GetLastError()));

    HINTERNET hConn = InternetConnect(hNet, OVERPASS_HOST, INTERNET_DEFAULT_HTTPS_PORT,
                                      NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn)
    {
        InternetCloseHandle(hNet);
        return std::unexpected(std::format("OSM fetch {}: InternetConnect failed ({})", icao, GetLastError()));
    }

    const std::string query   = BuildOverpassQuery(centerLat, centerLon, radiusM);
    const std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    const std::string body    = "data=" + UrlEncode(query);

    constexpr int MAX_ATTEMPTS = 3;
    std::string   lastError;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        HINTERNET   hReq  = HttpOpenRequest(hConn, "POST", OVERPASS_PATH, NULL, NULL, NULL, flags, 0);
        if (!hReq)
        {
            InternetCloseHandle(hConn);
            InternetCloseHandle(hNet);
            return std::unexpected(std::format("OSM fetch: HttpOpenRequest failed ({})", GetLastError()));
        }

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

        if (statusCode == 504)
        {
            InternetCloseHandle(hReq);
            lastError = std::format("OSM fetch: HTTP 504 (attempt {}/{})", attempt, MAX_ATTEMPTS);
            // Brief pause before retrying (runs on background thread so Sleep is safe).
            if (attempt < MAX_ATTEMPTS)
                Sleep(3000);
            continue;
        }

        if (statusCode != 200)
        {
            InternetCloseHandle(hReq);
            InternetCloseHandle(hConn);
            InternetCloseHandle(hNet);
            return std::unexpected(std::format("OSM fetch: HTTP status {}", statusCode));
        }

        char        buf[4096];
        DWORD       bytesRead;
        std::string response;
        while (InternetReadFile(hReq, buf, sizeof(buf), &bytesRead) && bytesRead)
            response.append(buf, bytesRead);

        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hNet);

        return ParseOsmJson(response);
    }

    InternetCloseHandle(hConn);
    InternetCloseHandle(hNet);
    return std::unexpected(lastError);
}

OsmResult loadCachedTaxiways(const std::string& icao)
{
    const std::filesystem::path path = std::filesystem::path(GetPluginDirectory()) / CacheFilename(icao);
    if (!std::filesystem::exists(path))
        return std::unexpected(std::string("OSM cache: no cache file found"));

    std::ifstream f(path);
    if (!f)
        return std::unexpected(std::string("OSM cache: failed to open cache file"));

    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseOsmJson(ss.str());
}
