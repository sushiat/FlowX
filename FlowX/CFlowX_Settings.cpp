/**
 * @file CFlowX_Settings.cpp
 * @brief Settings layer; loads and saves config.json, EuroScope settings, and window positions.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CFlowX_Settings.h"
#include "helpers.h"
#include "osm_taxiways.h"
#include "taxi_graph.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>
#include <unordered_set>
#include <semver/semver.hpp>

/// @brief Returns true if every element of the JSON array is a scalar (not object/array).
static bool isScalarArray(const nlohmann::json& j)
{
    if (!j.is_array())
        return false;
    for (const auto& v : j)
        if (v.is_object() || v.is_array())
            return false;
    return true;
}

/// @brief Returns true if every value in the JSON object is a scalar or scalar-array.
static bool isSimpleObject(const nlohmann::json& j)
{
    if (!j.is_object())
        return false;
    for (const auto& [k, v] : j.items())
        if (v.is_object() || (v.is_array() && !isScalarArray(v)))
            return false;
    return true;
}

/// @brief Returns true if j (object, ≤4 keys) should be inlined even though it contains
///        nested simple-objects as values (e.g. a holding-point with a polygon child).
///        Does NOT apply to plain simple-objects — those are already handled by isSimpleObject.
static bool isDeepInlineable(const nlohmann::json& j)
{
    if (!j.is_object() || j.size() > 4)
        return false;
    bool hasNestedObject = false;
    for (const auto& [k, v] : j.items())
    {
        if (v.is_array() && !isScalarArray(v))
            return false;
        if (v.is_object())
        {
            if (!isSimpleObject(v))
                return false; // only one level of nesting allowed
            hasNestedObject = true;
        }
    }
    return hasNestedObject; // pure simple-objects are handled by isSimpleObject already
}

/// @brief Renders a deep-inlineable object (one level of simple-object nesting) on one line.
static std::string fmtDeepInline(const nlohmann::json& j)
{
    std::string s     = "{ ";
    bool        first = true;
    for (const auto& [k, v] : j.items())
    {
        if (!first)
            s += ", ";
        s += nlohmann::json(k).dump() + ": ";
        if (v.is_object())
        {
            std::string s2 = "{ ";
            bool        f2 = true;
            for (const auto& [k2, v2] : v.items())
            {
                if (!f2)
                    s2 += ", ";
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
            std::string s     = "[";
            bool        first = true;
            for (const auto& v : j)
            {
                if (!first)
                    s += ", ";
                s += v.dump();
                first = false;
            }
            return s + "]";
        }
        // Array with complex elements: one per line
        std::string s     = "[\n";
        bool        first = true;
        for (const auto& v : j)
        {
            if (!first)
                s += ",\n";
            s += pad1 + compactJson(v, depth + 1);
            first = false;
        }
        return s + "\n" + pad + "]";
    }

    // Object: inline if all values are scalars/scalar-arrays (any key count),
    // or if ≤4 keys with exactly one level of simple-object nesting.
    if (isSimpleObject(j))
    {
        std::string s     = "{ ";
        bool        first = true;
        for (const auto& [k, v] : j.items())
        {
            if (!first)
                s += ", ";
            s += nlohmann::json(k).dump() + ": " + v.dump();
            first = false;
        }
        return s + " }";
    }
    if (isDeepInlineable(j))
        return fmtDeepInline(j);

    std::string s     = "{\n";
    bool        first = true;
    for (const auto& [k, v] : j.items())
    {
        if (!first)
            s += ",\n";
        s += pad1 + nlohmann::json(k).dump() + ": " + compactJson(v, depth + 1);
        first = false;
    }
    return s + "\n" + pad + "}";
}

CFlowX_Settings::CFlowX_Settings()
{
    this->LoadSettings();
    this->LoadConfig();
    this->LoadAircraftData();
    this->LoadGroundRadarStands();

    if (this->updateCheck)
    {
        this->latestVersion = std::async(FetchLatestVersion);
    }

    this->RefreshActiveRunways();
    this->StartOsmCacheLoad();
}

void CFlowX_Settings::RefreshActiveRunways()
{
    this->activeDepRunways.clear();
    this->activeArrRunways.clear();

    auto trimmed = [](const char* s) -> std::string
    {
        std::string r = s ? s : "";
        r.erase(0, r.find_first_not_of(' '));
        if (!r.empty())
            r.erase(r.find_last_not_of(' ') + 1);
        return r;
    };

    // Iterate all runway sector elements; restrict to configured airports only.
    auto el = SectorFileElementSelectFirst(EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY);
    while (el.IsValid())
    {
        const std::string apt = trimmed(el.GetAirportName());
        if (this->airports.count(apt))
        {
            for (int i = 0; i <= 1; ++i)
            {
                if (el.IsElementActive(true, i))
                    this->activeDepRunways.insert(trimmed(el.GetRunwayName(i)));
                if (el.IsElementActive(false, i))
                    this->activeArrRunways.insert(trimmed(el.GetRunwayName(i)));
            }
        }
        el = SectorFileElementSelectNext(el, EuroScopePlugIn::SECTOR_ELEMENT_RUNWAY);
    }

    auto join = [](const std::set<std::string>& s) -> std::string
    {
        std::string out;
        for (const auto& r : s)
        {
            if (!out.empty())
                out += '/';
            out += r;
        }
        return out;
    };
    this->LogMessage(std::format("Active runways - DEP: [{}]  ARR: [{}]",
                                 join(this->activeDepRunways), join(this->activeArrRunways)),
                     "TAXI");
}

void CFlowX_Settings::RebuildTaxiGraph()
{
    if (this->osmData.ways.empty() || this->airports.empty())
        return;
    if (this->IsGraphBusy())
        return; // previous build still running; it will complete and be picked up by PollGraphFuture

    // Capture by value so the background thread is fully independent of the main-thread state.
    OsmAirportData osmSnap = this->osmData;
    airport        apSnap  = this->airports.begin()->second;

    this->LogDebugMessage("Building TaxiGraph (background)", "TAXI");
    this->graphFuture_ = std::async(std::launch::async,
                                    [osmSnap = std::move(osmSnap), apSnap = std::move(apSnap)]() mutable
                                    {
                                        TaxiGraph g;
                                        g.Build(osmSnap, apSnap);
                                        return g;
                                    });
}

bool CFlowX_Settings::IsGraphBusy() const
{
    return this->graphFuture_.valid() &&
           this->graphFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
}

void CFlowX_Settings::PollGraphFuture()
{
    if (!this->graphFuture_.valid())
        return;
    if (this->graphFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    this->osmGraph = this->graphFuture_.get();
    this->LogDebugMessage(
        std::format("TaxiGraph ready: {} nodes", this->osmGraph.NodeCount()), "TAXI");
}

void CFlowX_Settings::StartOsmCacheLoad()
{
    this->LogDebugMessage("Loading taxiway cache from disk", "OSM");
    this->osmFuture = std::async(std::launch::async, loadCachedTaxiways);
}

void CFlowX_Settings::StartOsmFetch()
{
    if (this->IsOsmBusy())
        return;
    this->LogDebugMessage("Starting Overpass API fetch", "OSM");
    this->osmFuture = std::async(std::launch::async, fetchLOWWTaxiways);
}

bool CFlowX_Settings::IsOsmBusy() const
{
    return this->osmFuture.valid() &&
           this->osmFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
}

void CFlowX_Settings::PollOsmFuture()
{
    if (!this->osmFuture.valid())
        return;
    if (this->osmFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    auto result = this->osmFuture.get();
    if (!result.has_value())
    {
        this->LogDebugMessage(std::format("Failed: {}", result.error()), "OSM");
        return;
    }

    this->osmData = std::move(result.value());

    if (this->osmData.preAnnotated)
    {
        int hpCount = 0, isxCount = 0, twCount = 0, tlCount = 0;
        for (const auto& way : this->osmData.ways)
        {
            switch (way.type)
            {
            case AerowayType::Taxiway_HoldingPoint:
                hpCount++;
                break;
            case AerowayType::Taxiway_Intersection:
                isxCount++;
                break;
            case AerowayType::Taxiway:
                twCount++;
                break;
            case AerowayType::Taxilane:
                tlCount++;
                break;
            default:
                break;
            }
        }
        this->LogDebugMessage(
            std::format("Loaded {} ways ({} taxiways, {} taxilanes, {} HP, {} intersections), {} holding positions from cache",
                        this->osmData.ways.size(), twCount, tlCount, hpCount, isxCount,
                        this->osmData.holdingPositions.size()),
            "OSM");
    }
    else
    {
        // Pass 1: annotate any taxiway whose ref or name matches a configured holding point.
        for (auto& way : this->osmData.ways)
        {
            for (const auto& [icao, apt] : this->airports)
            {
                bool matched = false;
                for (const auto& [rwyName, rwy] : apt.runways)
                {
                    if ((!way.ref.empty() && rwy.holdingPoints.contains(way.ref)) ||
                        (!way.name.empty() && rwy.holdingPoints.contains(way.name)))
                    {
                        way.type = AerowayType::Taxiway_HoldingPoint;
                        matched  = true;
                        break;
                    }
                }
                if (matched)
                    break;
            }
        }

        // Build lookup sets from the first configured airport that has OSM lists.
        std::unordered_set<std::string> cfgTaxiways, cfgTaxilanes, cfgIntersections;
        for (const auto& [icao, apt] : this->airports)
        {
            if (!apt.taxiWays.empty() || !apt.taxiLanes.empty() || !apt.taxiIntersections.empty())
            {
                cfgTaxiways.insert(apt.taxiWays.begin(), apt.taxiWays.end());
                cfgTaxilanes.insert(apt.taxiLanes.begin(), apt.taxiLanes.end());
                cfgIntersections.insert(apt.taxiIntersections.begin(), apt.taxiIntersections.end());
                break;
            }
        }

        // Pass 2: classify by config lists; ways not in any list are removed.
        // Holding-point ways already annotated in Pass 1 keep their type.
        auto keyOf = [](const OsmWay& w) -> const std::string&
        { return w.ref.empty() ? w.name : w.ref; };
        {
            std::vector<OsmWay> kept;
            kept.reserve(this->osmData.ways.size());
            for (auto& way : this->osmData.ways)
            {
                // Holding-point and runway ways are kept as-is; no config entry required.
                if (way.type == AerowayType::Taxiway_HoldingPoint ||
                    way.type == AerowayType::Runway)
                {
                    kept.push_back(std::move(way));
                    continue;
                }
                const std::string& key = keyOf(way);
                if (cfgIntersections.contains(key))
                {
                    way.type = AerowayType::Taxiway_Intersection;
                    kept.push_back(std::move(way));
                }
                else if (cfgTaxiways.contains(key))
                {
                    way.type = AerowayType::Taxiway;
                    kept.push_back(std::move(way));
                }
                else if (cfgTaxilanes.contains(key))
                {
                    way.type = AerowayType::Taxilane;
                    kept.push_back(std::move(way));
                }
                // else: not in any config list → excluded
            }
            this->osmData.ways = std::move(kept);
        }

        int hpCount = 0, isxCount = 0, twCount = 0, tlCount = 0;
        for (const auto& way : this->osmData.ways)
        {
            switch (way.type)
            {
            case AerowayType::Taxiway_HoldingPoint:
                hpCount++;
                break;
            case AerowayType::Taxiway_Intersection:
                isxCount++;
                break;
            case AerowayType::Taxiway:
                twCount++;
                break;
            case AerowayType::Taxilane:
                tlCount++;
                break;
            default:
                break;
            }
        }
        this->LogDebugMessage(
            std::format("Annotated {} ways ({} taxiways, {} taxilanes, {} HP, {} intersections), {} holding positions; saving cache",
                        this->osmData.ways.size(), twCount, tlCount, hpCount, isxCount,
                        this->osmData.holdingPositions.size()),
            "OSM");

        // Delete stale cache before writing fresh data.
        DeleteOsmCache();
        SaveOsmCache(this->osmData);
    }

    this->RebuildTaxiGraph();
}

/// @brief Loads plugin settings (global toggles and window positions) from settings.json in the plugin directory.
void CFlowX_Settings::LoadSettings()
{
    try
    {
        std::filesystem::path path(GetPluginDirectory());
        path.append("settings.json");

        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            return;
        }

        json j = json::parse(ifs);

        if (j.contains("global"))
        {
            this->autoParked          = j["global"].value("autoParked", true);
            this->autoScratchpadClear = j["global"].value("autoScratchpadClear", false);
            this->autoRestore         = j["global"].value("autoRestore", false);
            this->hpAutoScratch       = j["global"].value("hpAutoScratch", true);
            this->bgOpacity           = j["global"].value("bgOpacity", 100);
            this->debug               = j["global"].value("debug", false);
            this->flashOnMessage      = j["global"].value("flashOnMessage", false);
            this->fontOffset          = j["global"].value("fontOffset", 0);
            this->soundAirborne       = j["global"].value("soundAirborne", true);
            this->soundGndTransfer    = j["global"].value("soundGndTransfer", true);
            this->soundReadyTakeoff   = j["global"].value("soundReadyTakeoff", true);
            this->soundNoRoute        = j["global"].value("soundNoRoute", true);
            this->soundTaxiConflict   = j["global"].value("soundTaxiConflict", true);
            this->updateCheck         = j["global"].value("updateCheck", true);
        }
        if (this->debug)
        {
            this->LogDebugSessionStart();
        }

        if (j.contains("windowSettings") && j["windowSettings"].is_array())
        {
            for (const auto& w : j["windowSettings"])
            {
                std::string name = w.value("name", "");
                if (name == "approachEstWindow")
                {
                    this->approachEstWindowX   = w.value("x", -1);
                    this->approachEstWindowY   = w.value("y", -1);
                    this->approachEstWindowW   = w.value("w", 180);
                    this->approachEstWindowH   = w.value("h", 380);
                    this->approachEstVisible   = w.value("visible", true);
                    this->apprEstColors        = w.value("apprEstColors", false);
                    this->approachEstPoppedOut = w.value("poppedOut", false);
                    this->approachEstPopoutX   = w.value("popoutX", -1);
                    this->approachEstPopoutY   = w.value("popoutY", -1);
                    this->approachEstPopoutW   = w.value("popoutW", -1);
                    this->approachEstPopoutH   = w.value("popoutH", -1);
                }
                else if (name == "depRateWindow")
                {
                    this->depRateWindowX   = w.value("x", -1);
                    this->depRateWindowY   = w.value("y", -1);
                    this->depRateVisible   = w.value("visible", true);
                    this->depRatePoppedOut = w.value("poppedOut", false);
                    this->depRatePopoutX   = w.value("popoutX", -1);
                    this->depRatePopoutY   = w.value("popoutY", -1);
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
                    this->weatherWindowX   = w.value("x", -1);
                    this->weatherWindowY   = w.value("y", -1);
                    this->weatherVisible   = w.value("visible", true);
                    this->weatherPoppedOut = w.value("poppedOut", false);
                    this->weatherPopoutX   = w.value("popoutX", -1);
                    this->weatherPopoutY   = w.value("popoutY", -1);
                }
            }
        }
    }
    catch (std::exception&)
    {
        // Missing or malformed file — positions stay at -1 (auto-place on first draw)
    }
}

/// @brief Writes plugin settings (global toggles and window positions) to settings.json in the plugin directory.
void CFlowX_Settings::SaveSettings()
{
    try
    {
        json j;
        j["global"]["autoParked"]          = this->autoParked;
        j["global"]["autoScratchpadClear"] = this->autoScratchpadClear;
        j["global"]["autoRestore"]         = this->autoRestore;
        j["global"]["hpAutoScratch"]       = this->hpAutoScratch;
        j["global"]["bgOpacity"]           = this->bgOpacity;
        j["global"]["debug"]               = this->debug;
        j["global"]["flashOnMessage"]      = this->flashOnMessage;
        j["global"]["fontOffset"]          = this->fontOffset;
        j["global"]["soundAirborne"]       = this->soundAirborne;
        j["global"]["soundGndTransfer"]    = this->soundGndTransfer;
        j["global"]["soundReadyTakeoff"]   = this->soundReadyTakeoff;
        j["global"]["soundNoRoute"]        = this->soundNoRoute;
        j["global"]["soundTaxiConflict"]   = this->soundTaxiConflict;
        j["global"]["updateCheck"]         = this->updateCheck;

        json windows = json::array();
        auto addWin  = [&](const char* name, int x, int y, bool vis)
        {
            json w;
            w["name"]    = name;
            w["x"]       = x;
            w["y"]       = y;
            w["visible"] = vis;
            windows.push_back(w);
        };
        {
            json w;
            w["name"]          = "approachEstWindow";
            w["x"]             = this->approachEstWindowX;
            w["y"]             = this->approachEstWindowY;
            w["w"]             = this->approachEstWindowW;
            w["h"]             = this->approachEstWindowH;
            w["visible"]       = this->approachEstVisible;
            w["apprEstColors"] = this->apprEstColors;
            w["poppedOut"]     = this->approachEstPoppedOut;
            w["popoutX"]       = this->approachEstPopoutX;
            w["popoutY"]       = this->approachEstPopoutY;
            w["popoutW"]       = this->approachEstPopoutW;
            w["popoutH"]       = this->approachEstPopoutH;
            windows.push_back(w);
        }
        {
            json w;
            w["name"]      = "depRateWindow";
            w["x"]         = this->depRateWindowX;
            w["y"]         = this->depRateWindowY;
            w["visible"]   = this->depRateVisible;
            w["poppedOut"] = this->depRatePoppedOut;
            w["popoutX"]   = this->depRatePopoutX;
            w["popoutY"]   = this->depRatePopoutY;
            windows.push_back(w);
        }
        {
            json w;
            w["name"]    = "twrOutboundWindow";
            w["x"]       = this->twrOutboundWindowX;
            w["y"]       = this->twrOutboundWindowY;
            w["visible"] = this->twrOutboundVisible;
            windows.push_back(w);
        }
        {
            json w;
            w["name"]    = "twrInboundWindow";
            w["x"]       = this->twrInboundWindowX;
            w["y"]       = this->twrInboundWindowY;
            w["visible"] = this->twrInboundVisible;
            windows.push_back(w);
        }
        {
            json w;
            w["name"]              = "napWindow";
            w["x"]                 = this->napWindowX;
            w["y"]                 = this->napWindowY;
            w["lastDismissedDate"] = this->napLastDismissedDate;
            windows.push_back(w);
        }
        {
            json w;
            w["name"]      = "weatherWindow";
            w["x"]         = this->weatherWindowX;
            w["y"]         = this->weatherWindowY;
            w["visible"]   = this->weatherVisible;
            w["poppedOut"] = this->weatherPoppedOut;
            w["popoutX"]   = this->weatherPopoutX;
            w["popoutY"]   = this->weatherPopoutY;
            windows.push_back(w);
        }
        j["windowSettings"] = windows;

        std::filesystem::path path(GetPluginDirectory());
        path.append("settings.json");

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
        std::map<std::string, double>                    wtcSum;
        std::map<std::string, int>                       wtcCount;
        std::vector<std::pair<std::string, std::string>> noWingspan; // {ICAO, WTC}

        for (auto& entry : j)
        {
            std::string icao = entry.value("ICAO", "");
            std::string wtc  = entry.value("WTC", "");
            if (icao.empty() || wtc.empty())
                continue;

            if (entry.contains("Wingspan"))
            {
                double ws                     = entry["Wingspan"].get<double>();
                this->aircraftWingspans[icao] = ws;
                wtcSum[wtc] += ws;
                wtcCount[wtc] += 1;
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
            if (dms.size() < 2)
                return 0.0;
            char hemi = dms[0];
            auto segs = split(dms.substr(1), '.');
            if (segs.size() < 4)
                return 0.0;
            double deg = std::stod(segs[0]);
            double min = std::stod(segs[1]);
            double sec = std::stod(segs[2]) + std::stod(segs[3]) / 1000.0;
            double val = deg + min / 60.0 + sec / 3600.0;
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
            if (line.empty() || line[0] == '/')
                continue;

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
            else if (inStand && starts_with(line, "HEADING:"))
            {
                // "HEADING:200"
                current.heading = std::stoi(line.substr(8));
            }
            else if (inStand && starts_with(line, "BLOCKS:"))
            {
                // "BLOCKS:B68"  |  "BLOCKS:B69:35.99"  |  "BLOCKS:A93,A96:31.99"
                std::string rest  = line.substr(7);
                auto        cp    = split(rest, ':');
                double      minWs = (cp.size() >= 2) ? std::stod(cp[1]) : 0.0;
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

        if (json_airport.contains("scratchpadClearExclusions"))
            ap.scratchpadClearExclusions = json_airport["scratchpadClearExclusions"].get<std::vector<std::string>>();
        if (json_airport.contains("taxiIntersections"))
            ap.taxiIntersections = json_airport["taxiIntersections"].get<std::vector<std::string>>();
        if (json_airport.contains("taxiLanes"))
            ap.taxiLanes = json_airport["taxiLanes"].get<std::vector<std::string>>();
        if (json_airport.contains("taxiWays"))
            ap.taxiWays = json_airport["taxiWays"].get<std::vector<std::string>>();
        if (json_airport.contains("taxiFlowGeneric"))
            for (const auto& r : json_airport["taxiFlowGeneric"])
                ap.taxiFlowGeneric.push_back({r.value("taxiway", std::string{}), r.value("direction", std::string{}), r.value("againstFlowMult", 0.0)});
        if (json_airport.contains("taxiFlowConfigs"))
        {
            // Normalise each key: split on '_', sort each side on '/', rejoin.
            // Allows the config file to use any ordering (e.g. "29/16_16" → stored as "16/29_16").
            auto normSide = [](const std::string& side) -> std::string
            {
                std::vector<std::string> parts;
                size_t                   start = 0, end;
                while ((end = side.find('/', start)) != std::string::npos)
                {
                    parts.push_back(side.substr(start, end - start));
                    start = end + 1;
                }
                parts.push_back(side.substr(start));
                std::ranges::sort(parts);
                std::string out;
                for (const auto& p : parts)
                {
                    if (!out.empty())
                        out += '/';
                    out += p;
                }
                return out;
            };
            for (auto& [rawKey, json_rules] : json_airport["taxiFlowConfigs"].items())
            {
                const auto  sep = rawKey.find('_');
                std::string normKey =
                    (sep == std::string::npos)
                        ? rawKey
                        : normSide(rawKey.substr(0, sep)) + '_' + normSide(rawKey.substr(sep + 1));
                for (const auto& r : json_rules)
                    ap.taxiFlowConfigs[normKey].push_back(
                        {r.value("taxiway", std::string{}), r.value("direction", std::string{}), r.value("againstFlowMult", 0.0)});
            }
        }
        if (json_airport.contains("taxiWingspanMax"))
            for (const auto& [ref, ws] : json_airport["taxiWingspanMax"].items())
                ap.taxiWingspanMax[ref] = ws.get<double>();
        if (json_airport.contains("taxiWingspanAvoid"))
            for (const auto& [ref, ws] : json_airport["taxiWingspanAvoid"].items())
                ap.taxiWingspanAvoid[ref] = ws.get<double>();
        if (json_airport.contains("taxiLaneSwingoverPairs"))
            for (const auto& pair : json_airport["taxiLaneSwingoverPairs"])
                if (pair.is_array() && pair.size() == 2)
                    ap.taxiLaneSwingoverPairs.push_back(
                        {pair[0].get<std::string>(), pair[1].get<std::string>()});

        if (json_airport.contains("taxiNetworkConfig"))
        {
            const auto& jnc = json_airport["taxiNetworkConfig"];
            auto&       nc  = ap.taxiNetworkConfig;
            if (jnc.contains("graph"))
            {
                const auto& g                    = jnc["graph"];
                nc.graph.subdivisionIntervalM    = g.value("subdivisionIntervalM", 15.0);
                nc.graph.osmHoldingPositionSnapM = g.value("osmHoldingPositionSnapM", 25.0);
                nc.graph.configHoldingPointSnapM = g.value("configHoldingPointSnapM", 40.0);
            }
            if (jnc.contains("edgeCosts"))
            {
                const auto& e                   = jnc["edgeCosts"];
                nc.edgeCosts.multIntersection   = e.value("multIntersection", 1.1);
                nc.edgeCosts.multTaxilane       = e.value("multTaxilane", 3.0);
                nc.edgeCosts.multRunway         = e.value("multRunway", 20.0);
                nc.edgeCosts.multRunwayApproach = e.value("multRunwayApproach", 18.0);
                nc.edgeCosts.multWingspanAvoid  = e.value("multWingspanAvoid", 3.0);
            }
            if (jnc.contains("flowRules"))
            {
                const auto& f                  = jnc["flowRules"];
                nc.flowRules.withFlowMaxDeg    = f.value("withFlowMaxDeg", 45.0);
                nc.flowRules.withFlowMult      = f.value("withFlowMult", 0.9);
                nc.flowRules.againstFlowMinDeg = f.value("againstFlowMinDeg", 135.0);
                nc.flowRules.againstFlowMult   = f.value("againstFlowMult", 3.0);
            }
            if (jnc.contains("routing"))
            {
                const auto& r                  = jnc["routing"];
                nc.routing.hardTurnDeg         = r.value("hardTurnDeg", 50.0);
                nc.routing.wayrefChangePenalty = r.value("wayrefChangePenalty", 200.0);
                nc.routing.forwardSnapM        = r.value("forwardSnapM", 120.0);
                nc.routing.backwardSnapM       = r.value("backwardSnapM", 300.0);
                nc.routing.heuristicWeight     = r.value("heuristicWeight", 1.0);
                nc.routing.maxNodeExpansions   = r.value("maxNodeExpansions", 5000);
                nc.routing.softTurnCostPerDeg  = r.value("softTurnCostPerDeg", 0.0);
            }
            if (jnc.contains("snapping"))
            {
                const auto& s               = jnc["snapping"];
                nc.snapping.holdingPointM   = s.value("holdingPointM", 30.0);
                nc.snapping.intersectionM   = s.value("intersectionM", 15.0);
                nc.snapping.suggestedRouteM = s.value("suggestedRouteM", 20.0);
                nc.snapping.waypointM       = s.value("waypointM", 40.0);
                nc.snapping.goalSnapM       = s.value("goalSnapM", 170.0);
            }
            if (jnc.contains("safety"))
            {
                const auto& sf             = jnc["safety"];
                nc.safety.deviationThreshM = sf.value("deviationThreshM", 40.0);
                nc.safety.minSpeedKt       = sf.value("minSpeedKt", 3.0);
                nc.safety.maxPredictS      = sf.value("maxPredictS", 60.0);
                nc.safety.conflictDeltaS   = sf.value("conflictDeltaS", 30.0);
                nc.safety.sameDirDeg       = sf.value("sameDirDeg", 45.0);
            }
        }

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

        if (json_airport.contains("taxiOnlyZones"))
        {
            for (auto& [name, json_zone] : json_airport.at("taxiOnlyZones").items())
            {
                taxiOutStands zone{name};
                zone.lat = json_zone["lat"].get<std::vector<double>>();
                zone.lon = json_zone["lon"].get<std::vector<double>>();
                ap.taxiOnlyZones.emplace(name, zone);
            }
        }

        if (json_airport.contains("standRoutingTargets"))
        {
            for (auto& [standName, hpLabel] : json_airport.at("standRoutingTargets").items())
                ap.standRoutingTargets.emplace(standName, hpLabel.get<std::string>());
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
                rwy.designator = rwyDesignator;
                {
                    std::string digits = rwyDesignator;
                    std::erase_if(digits, [](char c)
                                  { return !std::isdigit(c); });
                    rwy.headingNumber = digits.empty() ? -1 : std::stoi(digits);
                }
                rwy.opposite             = json_rwy.value<std::string>("opposite", "");
                rwy.thresholdLat         = json_rwy["threshold"].value<double>("lat", 0.0);
                rwy.thresholdLon         = json_rwy["threshold"].value<double>("lon", 0.0);
                rwy.twrFreq              = json_rwy.value<std::string>("twrFreq", "");
                rwy.goAroundFreq         = json_rwy.value<std::string>("goAroundFreq", "");
                rwy.thresholdElevationFt = json_rwy.value<int>("thresholdElevationFt", 0);
                rwy.widthMeters          = json_rwy.value<int>("width", 0);
                rwy.estimateBarSide      = json_rwy.value<std::string>("estimateBarSide", "");

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

                int hpOrder = 0;
                for (auto& [hpName, json_hp] : json_rwy["holdingPoints"].items())
                {
                    holdingPoint hp{};
                    hp.name       = hpName;
                    hp.assignable = json_hp.value<bool>("assignable", false);
                    hp.sameAs     = json_hp.value<std::string>("sameAs", "");
                    hp.order      = hpOrder++;
                    hp.lat        = json_hp["polygon"]["lat"].get<std::vector<double>>();
                    hp.lon        = json_hp["polygon"]["lon"].get<std::vector<double>>();
                    if (!hp.lat.empty())
                    {
                        hp.centerLat = std::ranges::fold_left(hp.lat, 0.0, std::plus{}) / hp.lat.size();
                        hp.centerLon = std::ranges::fold_left(hp.lon, 0.0, std::plus{}) / hp.lon.size();
                    }
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
                            af.name              = json_af.value<std::string>("name", "");
                            af.lat               = json_af.value<double>("lat", 0.0);
                            af.lon               = json_af.value<double>("lon", 0.0);
                            af.legType           = json_af.value<std::string>("legType", "straight");
                            af.legLengthNm       = json_af.value<double>("legLengthNm", 0.0);
                            af.detectionRadiusNm = json_af.value<double>("detectionRadiusNm", 0.0);
                            af.iafHeading        = json_af.value<int>("iafHeading", 0);
                            int altitude         = json_af.value<int>("altitude", 0);
                            int offsetBelow      = json_af.value<int>("altOffsetBelow", 0);
                            int offsetAbove      = json_af.value<int>("altOffsetAbove", 0);
                            af.altMinFt          = (altitude > 0 && offsetBelow > 0) ? altitude - offsetBelow : 0;
                            af.altMaxFt          = (altitude > 0 && offsetAbove > 0) ? altitude + offsetAbove : 0;
                            path.fixes.push_back(af);
                        }

                        // Derive arc centre and radius for each arc-type fix from its predecessor
                        for (size_t fi = 1; fi < path.fixes.size(); ++fi)
                        {
                            auto& fix = path.fixes[fi];
                            if ((fix.legType != "arcLeft" && fix.legType != "arcRight") || fix.legLengthNm <= 0.0)
                                continue;
                            const auto& prev = path.fixes[fi - 1];

                            // Flat-earth local NM coordinate system centred on prev
                            double latAvg = (prev.lat + fix.lat) * 0.5;
                            double cosLat = std::cos(latAvg * 3.141592653589793 / 180.0);
                            double dx     = (fix.lon - prev.lon) * cosLat * 60.0; // NM east
                            double dy     = (fix.lat - prev.lat) * 60.0;          // NM north
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
                                if (std::abs(du) < 1e-10)
                                    break;
                            }
                            double r = L / (2.0 * u);   // radius
                            double d = r * std::cos(u); // signed distance from chord midpoint to centre

                            // Chord unit vector and perpendicular (left = CCW 90°, right = CW 90°)
                            double ux = dx / chord, uy = dy / chord;
                            double perpX = (fix.legType == "arcRight") ? uy : -uy;
                            double perpY = (fix.legType == "arcRight") ? -ux : ux;

                            // Centre in NM relative to prev, then back to lat/lon
                            double cx        = dx * 0.5 + d * perpX;
                            double cy        = dy * 0.5 + d * perpY;
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
        if (f)
            f << ts << " [Config] config.json contents:\n"
              << compactJson(config) << "\n";
    }

    // Computed/derived values not present in raw JSON (arc geometry for GPS approach paths; holding-point centroids).
    for (auto& [icao, ap] : this->airports)
    {
        for (auto& [rwyDesignator, rwy] : ap.runways)
        {
            for (auto& [hpName, hp] : rwy.holdingPoints)
            {
                if (hp.centerLat == 0.0 && hp.centerLon == 0.0)
                {
                    continue;
                }
                this->LogDebugFileOnly(std::format(
                                           "[{}][{}][{}] hp centre={:.6f}/{:.6f}",
                                           icao, rwyDesignator, hpName,
                                           hp.centerLat, hp.centerLon),
                                       "Config");
            }

            for (auto& path : rwy.gpsApproachPaths)
            {
                for (auto& fix : path.fixes)
                {
                    if (fix.arcRadiusNm <= 0.0)
                    {
                        continue;
                    }
                    this->LogDebugFileOnly(std::format(
                                               "[{}][{}][{}][{}] arc: centre={:.6f}/{:.6f} r={:.3f}nm",
                                               icao, rwyDesignator, path.name, fix.name,
                                               fix.arcCenterLat, fix.arcCenterLon, fix.arcRadiusNm),
                                           "Config");
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
