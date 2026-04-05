/**
 * @file CFlowX_Settings.cpp
 * @brief Settings layer; loads and saves config.json, EuroScope settings, and window positions.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX_Settings.h"
#include "helpers.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <semver/semver.hpp>

/// @brief Returns true if every element of the JSON array is a scalar (not object/array).
static bool isScalarArray(const nlohmann::json& j)
{
    if (!j.is_array()) return false;
    for (const auto& v : j)
        if (v.is_object() || v.is_array()) return false;
    return true;
}

/// @brief Returns true if every value in the JSON object is a scalar or scalar-array.
static bool isSimpleObject(const nlohmann::json& j)
{
    if (!j.is_object()) return false;
    for (const auto& [k, v] : j.items())
        if (v.is_object() || (v.is_array() && !isScalarArray(v))) return false;
    return true;
}

/// @brief Returns true if j (object, ≤4 keys) should be inlined even though it contains
///        nested simple-objects as values (e.g. a holding-point with a polygon child).
///        Does NOT apply to plain simple-objects — those are already handled by isSimpleObject.
static bool isDeepInlineable(const nlohmann::json& j)
{
    if (!j.is_object() || j.size() > 4) return false;
    bool hasNestedObject = false;
    for (const auto& [k, v] : j.items())
    {
        if (v.is_array() && !isScalarArray(v)) return false;
        if (v.is_object())
        {
            if (!isSimpleObject(v)) return false;  // only one level of nesting allowed
            hasNestedObject = true;
        }
    }
    return hasNestedObject;  // pure simple-objects are handled by isSimpleObject already
}

/// @brief Renders a deep-inlineable object (one level of simple-object nesting) on one line.
static std::string fmtDeepInline(const nlohmann::json& j)
{
    std::string s = "{ ";
    bool first = true;
    for (const auto& [k, v] : j.items())
    {
        if (!first) s += ", ";
        s += nlohmann::json(k).dump() + ": ";
        if (v.is_object())
        {
            std::string s2 = "{ ";
            bool f2 = true;
            for (const auto& [k2, v2] : v.items())
            {
                if (!f2) s2 += ", ";
                s2 += nlohmann::json(k2).dump() + ": " + v2.dump();
                f2 = false;
            }
            s += s2 + " }";
        }
        else
            s += v.dump();
        first = false;
    }
    return s + " }";
}

/// @brief Produces compact JSON: scalar arrays, simple objects, and small objects with one level
///        of simple-object nesting are written on one line; everything else is pretty-printed.
static std::string compactJson(const nlohmann::json& j, int depth = 0)
{
    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    const std::string pad1(static_cast<size_t>(depth + 1) * 2, ' ');

    if (!j.is_object() && !j.is_array())
        return j.dump();

    if (j.is_array())
    {
        if (isScalarArray(j))
        {
            std::string s = "[";
            bool first = true;
            for (const auto& v : j) { if (!first) s += ", "; s += v.dump(); first = false; }
            return s + "]";
        }
        // Array with complex elements: one per line
        std::string s = "[\n";
        bool first = true;
        for (const auto& v : j)
        {
            if (!first) s += ",\n";
            s += pad1 + compactJson(v, depth + 1);
            first = false;
        }
        return s + "\n" + pad + "]";
    }

    // Object: inline if all values are scalars/scalar-arrays (any key count),
    // or if ≤4 keys with exactly one level of simple-object nesting.
    if (isSimpleObject(j))
    {
        std::string s = "{ ";
        bool first = true;
        for (const auto& [k, v] : j.items())
        {
            if (!first) s += ", ";
            s += nlohmann::json(k).dump() + ": " + v.dump();
            first = false;
        }
        return s + " }";
    }
    if (isDeepInlineable(j))
        return fmtDeepInline(j);

    std::string s = "{\n";
    bool first = true;
    for (const auto& [k, v] : j.items())
    {
        if (!first) s += ",\n";
        s += pad1 + nlohmann::json(k).dump() + ": " + compactJson(v, depth + 1);
        first = false;
    }
    return s + "\n" + pad + "}";
}

CFlowX_Settings::CFlowX_Settings()
{
    this->LoadWindowSettings();
    this->LoadConfig();
    this->LoadAircraftData();
    this->LoadGroundRadarStands();

    if (this->updateCheck)
    {
        this->latestVersion = std::async(FetchLatestVersion);
    }
}

/// @brief Loads window positions and global UI settings from windowSettings.json in the plugin directory.
void CFlowX_Settings::LoadWindowSettings()
{
    try
    {
        std::filesystem::path path(GetPluginDirectory());
        path.append("windowSettings.json");

        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            return;
        }

        json j = json::parse(ifs);

        if (j.contains("global"))
        {
            this->autoParked       = j["global"].value("autoParked",      true);
            this->autoRestore      = j["global"].value("autoRestore",     false);
            this->bgOpacity        = j["global"].value("bgOpacity",       100);
            this->debug            = j["global"].value("debug",           false);
            this->flashOnMessage   = j["global"].value("flashOnMessage",  false);
            this->fontOffset       = j["global"].value("fontOffset",      0);
            this->updateCheck      = j["global"].value("updateCheck",     true);
        }
        if (this->debug) { this->LogDebugSessionStart(); }

        if (j.contains("windowSettings") && j["windowSettings"].is_array())
        {
            for (const auto& w : j["windowSettings"])
            {
                std::string name = w.value("name", "");
                if (name == "depRateWindow")
                {
                    this->depRateWindowX = w.value("x", -1);
                    this->depRateWindowY = w.value("y", -1);
                    this->depRateVisible = w.value("visible", true);
                }
                else if (name == "twrOutboundWindow")
                {
                    this->twrOutboundWindowX = w.value("x", -1);
                    this->twrOutboundWindowY = w.value("y", -1);
                    this->twrOutboundVisible = w.value("visible", true);
                }
                else if (name == "twrInboundWindow")
                {
                    this->twrInboundWindowX = w.value("x", -1);
                    this->twrInboundWindowY = w.value("y", -1);
                    this->twrInboundVisible = w.value("visible", true);
                }
                else if (name == "napWindow")
                {
                    this->napWindowX           = w.value("x", -1);
                    this->napWindowY           = w.value("y", -1);
                    this->napLastDismissedDate = w.value("lastDismissedDate", "");
                }
                else if (name == "weatherWindow")
                {
                    this->weatherWindowX = w.value("x", -1);
                    this->weatherWindowY = w.value("y", -1);
                    this->weatherVisible = w.value("visible", true);
                }
            }
        }
    }
    catch (std::exception&)
    {
        // Missing or malformed file — positions stay at -1 (auto-place on first draw)
    }
}

/// @brief Writes window positions and global UI settings to windowSettings.json in the plugin directory.
void CFlowX_Settings::SaveWindowSettings()
{
    try
    {
        json j;
        j["global"]["autoParked"]     = this->autoParked;
        j["global"]["autoRestore"]    = this->autoRestore;
        j["global"]["bgOpacity"]      = this->bgOpacity;
        j["global"]["debug"]          = this->debug;
        j["global"]["flashOnMessage"] = this->flashOnMessage;
        j["global"]["fontOffset"]     = this->fontOffset;
        j["global"]["updateCheck"]    = this->updateCheck;

        json windows = json::array();
        auto addWin = [&](const char* name, int x, int y, bool vis)
        {
            json w;
            w["name"]    = name;
            w["x"]       = x;
            w["y"]       = y;
            w["visible"] = vis;
            windows.push_back(w);
        };
        addWin("depRateWindow",     this->depRateWindowX,     this->depRateWindowY,     this->depRateVisible);
        addWin("twrOutboundWindow", this->twrOutboundWindowX, this->twrOutboundWindowY, this->twrOutboundVisible);
        addWin("twrInboundWindow",  this->twrInboundWindowX,  this->twrInboundWindowY,  this->twrInboundVisible);
        {
            json w;
            w["name"]              = "napWindow";
            w["x"]                 = this->napWindowX;
            w["y"]                 = this->napWindowY;
            w["lastDismissedDate"] = this->napLastDismissedDate;
            windows.push_back(w);
        }
        addWin("weatherWindow", this->weatherWindowX, this->weatherWindowY, this->weatherVisible);
        j["windowSettings"] = windows;

        std::filesystem::path path(GetPluginDirectory());
        path.append("windowSettings.json");

        std::ofstream ofs(path);
        ofs << j.dump(4);
    }
    catch (std::exception& e)
    {
        this->LogMessage("Failed to save window settings. Error: " + std::string(e.what()), "Settings");
    }
}

/// @brief Loads ICAO_Aircraft.json from the adjacent Groundradar folder into aircraftWingspans.
void CFlowX_Settings::LoadAircraftData()
{
    try
    {
        std::filesystem::path path(GetPluginDirectory());
        path = path.parent_path() / "Groundradar" / "ICAO_Aircraft.json";

        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            this->LogMessage("ICAO_Aircraft.json not found at: " + path.string(), "AircraftData");
            return;
        }

        json j = json::parse(ifs);

        // First pass: collect wingspans and accumulate per-WTC sums for average computation.
        std::map<std::string, double> wtcSum;
        std::map<std::string, int>    wtcCount;
        std::vector<std::pair<std::string, std::string>> noWingspan; // {ICAO, WTC}

        for (auto& entry : j)
        {
            std::string icao = entry.value("ICAO", "");
            std::string wtc  = entry.value("WTC", "");
            if (icao.empty() || wtc.empty())
                continue;

            if (entry.contains("Wingspan"))
            {
                double ws                       = entry["Wingspan"].get<double>();
                this->aircraftWingspans[icao]   = ws;
                wtcSum[wtc]                    += ws;
                wtcCount[wtc]                  += 1;
            }
            else
            {
                noWingspan.emplace_back(icao, wtc);
            }
        }

        // Compute per-WTC averages.
        std::map<std::string, double> wtcAvg;
        for (auto& [wtc, sum] : wtcSum)
            wtcAvg[wtc] = sum / wtcCount[wtc];

        // Second pass: fill missing wingspans with the WTC average.
        for (auto& [icao, wtc] : noWingspan)
        {
            auto it = wtcAvg.find(wtc);
            if (it != wtcAvg.end())
                this->aircraftWingspans[icao] = it->second;
        }

        this->LogMessage(
            std::format("Loaded wingspan data for {} aircraft types ({} used WTC average).",
                this->aircraftWingspans.size(), noWingspan.size()),
            "AircraftData");
    }
    catch (std::exception& e)
    {
        this->LogMessage("Failed to load ICAO_Aircraft.json: " + std::string(e.what()), "AircraftData");
    }
}

/// @brief Loads GRpluginStands.txt from the adjacent Groundradar folder into grStands.
void CFlowX_Settings::LoadGroundRadarStands()
{
    try
    {
        std::filesystem::path path(GetPluginDirectory());
        path = path.parent_path() / "Groundradar" / "GRpluginStands.txt";

        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            this->LogMessage("GRpluginStands.txt not found at: " + path.string(), "Stands");
            return;
        }

        // Parses one DMS coordinate token: N048.07.14.709 or E016.33.00.259
        auto parseDMS = [](const std::string& dms) -> double
        {
            if (dms.size() < 2) return 0.0;
            char   hemi = dms[0];
            auto   segs = split(dms.substr(1), '.');
            if (segs.size() < 4) return 0.0;
            double deg  = std::stod(segs[0]);
            double min  = std::stod(segs[1]);
            double sec  = std::stod(segs[2]) + std::stod(segs[3]) / 1000.0;
            double val  = deg + min / 60.0 + sec / 3600.0;
            return (hemi == 'S' || hemi == 'W') ? -val : val;
        };

        grStand current;
        bool    inStand = false;

        auto saveCurrentStand = [&]()
        {
            if (inStand && current.lat.size() >= 3)
                this->grStands[current.icao + ":" + current.name] = current;
        };

        std::string line;
        while (std::getline(ifs, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '/') continue;

            if (starts_with(line, "STAND:"))
            {
                saveCurrentStand();
                current = {};
                inStand = true;
                // "STAND:LOWW:B67" — split into three parts
                auto parts = split(line, ':');
                if (parts.size() >= 3)
                {
                    current.icao = parts[1];
                    current.name = parts[2];
                }
            }
            else if (inStand && starts_with(line, "COORD:"))
            {
                // "COORD:N048.07.14.709:E016.33.00.259"
                auto coords = split(line.substr(6), ':');
                if (coords.size() >= 2)
                {
                    current.lat.push_back(parseDMS(coords[0]));
                    current.lon.push_back(parseDMS(coords[1]));
                }
            }
            else if (inStand && starts_with(line, "BLOCKS:"))
            {
                // "BLOCKS:B68"  |  "BLOCKS:B69:35.99"  |  "BLOCKS:A93,A96:31.99"
                std::string rest      = line.substr(7);
                auto        cp        = split(rest, ':');
                double      minWs     = (cp.size() >= 2) ? std::stod(cp[1]) : 0.0;
                for (auto& sn : split(cp[0], ','))
                    current.blocks.push_back({trim(sn), minWs});
            }
        }
        saveCurrentStand();

        this->LogMessage(std::format("Loaded {} stands from GRpluginStands.txt.", this->grStands.size()), "Stands");
    }
    catch (std::exception& e)
    {
        this->LogMessage("Failed to load GRpluginStands.txt: " + std::string(e.what()), "Stands");
    }
}

/// @brief Parses config.json from the plugin directory and populates the airports map.
void CFlowX_Settings::LoadConfig()
{
    json config;
    try
    {
        std::filesystem::path base(GetPluginDirectory());
        base.append("config.json");

        std::ifstream ifs(base.c_str());

        config = json::parse(ifs);
    }
    catch (std::exception& e)
    {
        this->LogMessage("Failed to read config. Error: " + std::string(e.what()), "Config");
        return;
    }

    for (auto& [icao, json_airport] : config.items())
    {
        // Get basic airport attributes
        airport ap{
            icao,
            json_airport.value<std::string>("gndFreq", "")};
        ap.fieldElevation          = json_airport.value<int>("fieldElevation", 0);
        ap.airborneTransfer        = json_airport.value<int>("airborneTransfer", 0);
        ap.airborneTransferWarning = json_airport.value<int>("airborneTransferWarning", 0);

        auto ctrStations{json_airport["ctrStations"].get<std::vector<std::string>>()};
        ap.ctrStations = ctrStations;

        json json_geoGnds;
        try
        {
            json_geoGnds = json_airport.at("geoGndFreq");
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to get geographic ground frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
            continue;
        }

        for (auto& [name, json_geoGnd] : json_geoGnds.items())
        {
            geoGndFreq ggf{
                name,
                json_geoGnd.value<std::string>("freq", "")};

            auto lat{json_geoGnd["lat"].get<std::vector<double>>()};
            auto lon{json_geoGnd["lon"].get<std::vector<double>>()};
            ggf.lat = lat;
            ggf.lon = lon;

            ap.geoGndFreq.emplace(name, ggf);
        }

        json json_taxiouts;
        try
        {
            json_taxiouts = json_airport.at("taxiOutStands");
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to get taxi out stands for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
            continue;
        }

        for (auto& [name, json_taxiout] : json_taxiouts.items())
        {
            taxiOutStands tos{name};

            auto lat{json_taxiout["lat"].get<std::vector<double>>()};
            auto lon{json_taxiout["lon"].get<std::vector<double>>()};
            tos.lat = lat;
            tos.lon = lon;

            ap.taxiOutStands.emplace(name, tos);
        }

        json json_nap_reminder;
        try
        {
            json_nap_reminder = json_airport.at("napReminder");
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load NAP reminder config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
            continue;
        }

        napReminder reminder{
            json_nap_reminder.value<bool>("enabled", false),
            json_nap_reminder.value<int>("hour", 0),
            json_nap_reminder.value<int>("minute", 0),
            json_nap_reminder.value<std::string>("tzone", ""),
            false};
        ap.nap_reminder = reminder;

        // Load night SIDs
        try
        {
            for (auto& [sidKey, nightName] : json_airport.at("nightTimeSids").items())
            {
                ap.nightTimeSids.emplace(sidKey, nightName.get<std::string>());
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load night-time SID replacements for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
        }

        // Load SID-specific approach frequencies
        try
        {
            ap.defaultAppFreq = json_airport.value<std::string>("defaultAppFreq", "");
            for (auto& [freq, sids] : json_airport.at("sidAppFreqs").items())
            {
                ap.sidAppFreqs.emplace(freq, sids.get<std::vector<std::string>>());
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load SID-specific frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
        }

        try
        {
            for (auto& [freq, fallbacks] : json_airport.at("appFreqFallbacks").items())
            {
                ap.appFreqFallbacks.emplace(freq, fallbacks.get<std::vector<std::string>>());
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load approach frequency fallbacks for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
        }

        // Load runway configurations
        try
        {
            for (auto& [rwyDesignator, json_rwy] : json_airport.at("runways").items())
            {
                runway rwy{};
                rwy.designator   = rwyDesignator;
                rwy.opposite     = json_rwy.value<std::string>("opposite", "");
                rwy.thresholdLat = json_rwy["threshold"].value<double>("lat", 0.0);
                rwy.thresholdLon = json_rwy["threshold"].value<double>("lon", 0.0);
                rwy.twrFreq      = json_rwy.value<std::string>("twrFreq", "");
                rwy.goAroundFreq = json_rwy.value<std::string>("goAroundFreq", "");
                rwy.thresholdElevationFt = json_rwy.value<int>("thresholdElevationFt", 0);
                rwy.widthMeters          = json_rwy.value<int>("width", 0);

                for (auto& [groupKey, sids] : json_rwy["sidGroups"].items())
                {
                    int groupNum = std::stoi(groupKey);
                    for (auto& sid : sids)
                        rwy.sidGroups.emplace(sid.get<std::string>(), groupNum);
                }

                for (auto& [color, sids] : json_rwy["sidColors"].items())
                {
                    for (auto& sid : sids)
                        rwy.sidColors.emplace(sid.get<std::string>(), color);
                }

                for (auto& [hpName, json_hp] : json_rwy["holdingPoints"].items())
                {
                    holdingPoint hp{};
                    hp.name       = hpName;
                    hp.assignable = json_hp.value<bool>("assignable", false);
                    hp.sameAs     = json_hp.value<std::string>("sameAs", "");
                    hp.lat        = json_hp["polygon"]["lat"].get<std::vector<double>>();
                    hp.lon        = json_hp["polygon"]["lon"].get<std::vector<double>>();
                    rwy.holdingPoints.emplace(hpName, hp);
                }

                if (json_rwy.contains("vacatePoints"))
                {
                    for (auto& [vpName, json_vp] : json_rwy["vacatePoints"].items())
                    {
                        vacatePoint vp{};
                        vp.minGap = json_vp.value<double>("minGap", 0.0);
                        vp.stands = json_vp["stands"].get<std::vector<std::string>>();
                        rwy.vacatePoints.emplace(vpName, vp);
                    }
                }

                if (json_rwy.contains("gpsApproachPaths"))
                {
                    for (const auto& json_path : json_rwy["gpsApproachPaths"])
                    {
                        approachPath path{};
                        path.name = json_path.value<std::string>("name", "");
                        for (const auto& json_af : json_path["fixes"])
                        {
                            approachFix af{};
                            af.name               = json_af.value<std::string>("name", "");
                            af.lat                = json_af.value<double>("lat", 0.0);
                            af.lon                = json_af.value<double>("lon", 0.0);
                            af.legType            = json_af.value<std::string>("legType", "straight");
                            af.legLengthNm        = json_af.value<double>("legLengthNm", 0.0);
                            af.detectionRadiusNm  = json_af.value<double>("detectionRadiusNm", 0.0);
                            af.iafHeading         = json_af.value<int>("iafHeading", 0);
                            int altitude     = json_af.value<int>("altitude", 0);
                            int offsetBelow  = json_af.value<int>("altOffsetBelow", 0);
                            int offsetAbove  = json_af.value<int>("altOffsetAbove", 0);
                            af.altMinFt = (altitude > 0 && offsetBelow > 0) ? altitude - offsetBelow : 0;
                            af.altMaxFt = (altitude > 0 && offsetAbove > 0) ? altitude + offsetAbove : 0;
                            path.fixes.push_back(af);
                        }

                        // Derive arc centre and radius for each arc-type fix from its predecessor
                        for (size_t fi = 1; fi < path.fixes.size(); ++fi)
                        {
                            auto& fix = path.fixes[fi];
                            if ((fix.legType != "arcLeft" && fix.legType != "arcRight") || fix.legLengthNm <= 0.0) continue;
                            const auto& prev = path.fixes[fi - 1];

                            // Flat-earth local NM coordinate system centred on prev
                            double latAvg = (prev.lat + fix.lat) * 0.5;
                            double cosLat = std::cos(latAvg * 3.141592653589793 / 180.0);
                            double dx     = (fix.lon - prev.lon) * cosLat * 60.0; // NM east
                            double dy     = (fix.lat - prev.lat) * 60.0;           // NM north
                            double chord  = std::sqrt(dx * dx + dy * dy);
                            double L      = fix.legLengthNm;

                            // Solve sin(u)/u = chord/L for u = half-subtended-angle (Newton-Raphson)
                            double ratio = chord / L;
                            double u     = 1.0;
                            for (int iter = 0; iter < 30; ++iter)
                            {
                                double fu  = std::sin(u) / u - ratio;
                                double dfu = (std::cos(u) * u - std::sin(u)) / (u * u);
                                double du  = -fu / dfu;
                                u += du;
                                if (std::abs(du) < 1e-10) break;
                            }
                            double r = L / (2.0 * u);   // radius
                            double d = r * std::cos(u); // signed distance from chord midpoint to centre

                            // Chord unit vector and perpendicular (left = CCW 90°, right = CW 90°)
                            double ux = dx / chord, uy = dy / chord;
                            double perpX = (fix.legType == "arcRight") ?  uy : -uy;
                            double perpY = (fix.legType == "arcRight") ? -ux :  ux;

                            // Centre in NM relative to prev, then back to lat/lon
                            double cx = dx * 0.5 + d * perpX;
                            double cy = dy * 0.5 + d * perpY;
                            fix.arcCenterLat = prev.lat + cy / 60.0;
                            fix.arcCenterLon = prev.lon + cx / (cosLat * 60.0);
                            fix.arcRadiusNm  = r;
                        }

                        rwy.gpsApproachPaths.push_back(path);
                    }
                }

                ap.runways.emplace(rwyDesignator, rwy);
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load runway config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
        }

        this->airports.emplace(icao, ap);
    }

    this->LogMessage(std::format("Successfully loaded config for {} airport(s).", this->airports.size()), "Config");

    // Dump the raw JSON to the debug log file only (too verbose for chat).
    // Header line has a timestamp prefix; the JSON body is written as-is for easy copy/paste.
    if (this->debug)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[20];
        sprintf_s(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::filesystem::path logPath = std::filesystem::path(GetPluginDirectory()) / "debugLog.txt";
        std::ofstream         f(logPath, std::ios::app);
        if (f) f << ts << " [Config] config.json contents:\n" << compactJson(config) << "\n";
    }

    // Computed/derived values not present in raw JSON (arc geometry for GPS approach paths).
    for (auto& [icao, ap] : this->airports)
    {
        for (auto& [rwyDesignator, rwy] : ap.runways)
        {
            for (auto& path : rwy.gpsApproachPaths)
            {
                for (auto& fix : path.fixes)
                {
                    if (fix.arcRadiusNm <= 0.0) { continue; }
                    this->LogDebugFileOnly(std::format(
                        "[{}][{}][{}][{}] arc: centre={:.6f}/{:.6f} r={:.3f}nm",
                        icao, rwyDesignator, path.name, fix.name,
                        fix.arcCenterLat, fix.arcCenterLon, fix.arcRadiusNm), "Config");
                }
            }
        }
    }
}

/// @brief Resolves the latestVersion future and logs a message if a newer version is available.
void CFlowX_Settings::CheckForUpdate()
{
    try
    {
        semver::version latest{this->latestVersion.get()};
        semver::version current{PLUGIN_VERSION};

        if (latest > current)
        {
            std::string info = "A new version (" + latest.to_string() + ") of " + PLUGIN_NAME + " is available, download it at " + PLUGIN_LATEST_DOWNLOAD_URL;
            this->LogMessage(info, "Update");
        }
    }
    catch (std::exception& e)
    {
        MessageBoxA(NULL, e.what(), PLUGIN_NAME, MB_OK | MB_ICONERROR);
    }

    this->latestVersion = std::future<std::string>();
}
