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

CFlowX_Settings::CFlowX_Settings()
{
    this->updateCheck = false;

    this->LoadSettings();
    this->LoadWindowLocations();
    this->LoadConfig();
    this->LoadAircraftData();
    this->LoadGroundRadarStands();

    if (this->updateCheck)
    {
        this->latestVersion = std::async(FetchLatestVersion);
    }
}

/// @brief Loads persisted plugin settings from EuroScope's settings store.
void CFlowX_Settings::LoadSettings()
{
    const char* settings = this->GetDataFromSettings(PLUGIN_NAME);
    if (settings)
    {
        std::vector<std::string> splitSettings = split(settings, SETTINGS_DELIMITER);

        if (splitSettings.size() < 3)
        {
            this->LogMessage("Invalid saved settings found, reverting to default.", "Settings");

            this->SaveSettings();

            return;
        }

        std::istringstream(splitSettings[0]) >> this->updateCheck;
        std::istringstream(splitSettings[1]) >> this->flashOnMessage;
        std::istringstream(splitSettings[2]) >> this->debug;
        if (splitSettings.size() >= 4)
        {
            std::istringstream(splitSettings[3]) >> this->autoRestore;
        }
        this->LogMessage("Successfully loaded settings.", "Settings");
    }
    else
    {
        this->LogMessage("No saved settings found, using defaults.", "Settings");
    }
}

/// @brief Serialises current settings and writes them to EuroScope's settings store.
void CFlowX_Settings::SaveSettings()
{
    std::ostringstream ss;
    ss << this->updateCheck << SETTINGS_DELIMITER
       << this->flashOnMessage << SETTINGS_DELIMITER
       << this->debug << SETTINGS_DELIMITER
       << this->autoRestore;

    this->SaveDataToSettings(PLUGIN_NAME, "FlowX settings", ss.str().c_str());
}

/// @brief Loads window positions from windowLocations.json in the plugin directory.
void CFlowX_Settings::LoadWindowLocations()
{
    try
    {
        std::filesystem::path path(GetPluginDirectory());
        path.append("windowLocations.json");

        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            return;
        }

        json j = json::parse(ifs);

        if (j.contains("depRateWindow"))
        {
            this->depRateWindowX = j["depRateWindow"].value("x", -1);
            this->depRateWindowY = j["depRateWindow"].value("y", -1);
            this->depRateVisible = j["depRateWindow"].value("visible", true);
        }
        if (j.contains("twrOutboundWindow"))
        {
            this->twrOutboundWindowX = j["twrOutboundWindow"].value("x", -1);
            this->twrOutboundWindowY = j["twrOutboundWindow"].value("y", -1);
            this->twrOutboundVisible = j["twrOutboundWindow"].value("visible", true);
        }
        if (j.contains("twrInboundWindow"))
        {
            this->twrInboundWindowX = j["twrInboundWindow"].value("x", -1);
            this->twrInboundWindowY = j["twrInboundWindow"].value("y", -1);
            this->twrInboundVisible = j["twrInboundWindow"].value("visible", true);
        }
        if (j.contains("napWindow"))
        {
            this->napWindowX           = j["napWindow"].value("x", -1);
            this->napWindowY           = j["napWindow"].value("y", -1);
            this->napLastDismissedDate = j["napWindow"].value("lastDismissedDate", "");
        }
        if (j.contains("weatherWindow"))
        {
            this->weatherWindowX = j["weatherWindow"].value("x", -1);
            this->weatherWindowY = j["weatherWindow"].value("y", -1);
            this->weatherVisible = j["weatherWindow"].value("visible", true);
        }
    }
    catch (std::exception&)
    {
        // Missing or malformed file — positions stay at -1 (auto-place on first draw)
    }
}

/// @brief Writes current window positions to windowLocations.json in the plugin directory.
void CFlowX_Settings::SaveWindowLocations()
{
    try
    {
        json j;
        j["depRateWindow"]["x"]              = this->depRateWindowX;
        j["depRateWindow"]["y"]              = this->depRateWindowY;
        j["depRateWindow"]["visible"]        = this->depRateVisible;
        j["twrOutboundWindow"]["x"]          = this->twrOutboundWindowX;
        j["twrOutboundWindow"]["y"]          = this->twrOutboundWindowY;
        j["twrOutboundWindow"]["visible"]    = this->twrOutboundVisible;
        j["twrInboundWindow"]["x"]           = this->twrInboundWindowX;
        j["twrInboundWindow"]["y"]           = this->twrInboundWindowY;
        j["twrInboundWindow"]["visible"]     = this->twrInboundVisible;
        j["napWindow"]["x"]                  = this->napWindowX;
        j["napWindow"]["y"]                  = this->napWindowY;
        j["napWindow"]["lastDismissedDate"]  = this->napLastDismissedDate;
        j["weatherWindow"]["x"]              = this->weatherWindowX;
        j["weatherWindow"]["y"]              = this->weatherWindowY;
        j["weatherWindow"]["visible"]        = this->weatherVisible;

        std::filesystem::path path(GetPluginDirectory());
        path.append("windowLocations.json");

        std::ofstream ofs(path);
        ofs << j.dump(4);
    }
    catch (std::exception& e)
    {
        this->LogMessage("Failed to save window locations. Error: " + std::string(e.what()), "Settings");
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
            "Loaded wingspan data for " + std::to_string(this->aircraftWingspans.size()) +
            " aircraft types (" + std::to_string(noWingspan.size()) + " used WTC average).",
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

        this->LogMessage("Loaded " + std::to_string(this->grStands.size()) + " stands from GRpluginStands.txt.", "Stands");
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
                rwy.widthMeters  = json_rwy.value<int>("width", 0);

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

                ap.runways.emplace(rwyDesignator, rwy);
            }
        }
        catch (std::exception& e)
        {
            this->LogMessage("Failed to load runway config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
        }

        this->airports.emplace(icao, ap);
    }

    this->LogMessage("Successfully loaded config for " + std::to_string(this->airports.size()) + " airport(s).", "Config");

    for (auto& airport : this->airports)
    {
        this->LogDebugMessage("Airport: " + airport.first, "Config");
        this->LogDebugMessage("--> GND: " + airport.second.gndFreq, "Config");
        int ctrIndex = 0;
        for (const auto& ctr : airport.second.ctrStations)
        {
            this->LogDebugMessage("--> CTR[" + std::to_string(ctrIndex) + "]: " + ctr, "Config");
            ctrIndex++;
        }
        for (auto& geoGnd : airport.second.geoGndFreq)
        {
            this->LogDebugMessage("--> GeoGnd " + geoGnd.first, "Config");
            this->LogDebugMessage("----> FRQ: " + geoGnd.second.freq, "Config");
            std::string lat_string = std::accumulate(std::begin(geoGnd.second.lat), std::end(geoGnd.second.lat), std::string(),
                                                     [](const std::string& ss, const double s)
                                                     {
                                                         return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
                                                     });
            this->LogDebugMessage("----> LAT: " + lat_string, "Config");
            std::string lon_string = std::accumulate(std::begin(geoGnd.second.lon), std::end(geoGnd.second.lon), std::string(),
                                                     [](const std::string& ss, const double s)
                                                     {
                                                         return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
                                                     });
            this->LogDebugMessage("----> LON: " + lon_string, "Config");
        }
        for (auto& rwy : airport.second.runways)
        {
            this->LogDebugMessage("--> RWY[" + rwy.first + "] TWR: " + rwy.second.twrFreq + ", GA: " + rwy.second.goAroundFreq, "Config");
        }
        for (auto& taxiOut : airport.second.taxiOutStands)
        {
            this->LogDebugMessage("--> TaxiOut " + taxiOut.first, "Config");
            std::string lat_string = std::accumulate(std::begin(taxiOut.second.lat), std::end(taxiOut.second.lat), std::string(),
                                                     [](const std::string& ss, const double s)
                                                     {
                                                         return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
                                                     });
            this->LogDebugMessage("----> LAT: " + lat_string, "Config");
            std::string lon_string = std::accumulate(std::begin(taxiOut.second.lon), std::end(taxiOut.second.lon), std::string(),
                                                     [](const std::string& ss, const double s)
                                                     {
                                                         return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
                                                     });
            this->LogDebugMessage("----> LON: " + lon_string, "Config");
        }
        this->LogDebugMessage("---> NAP reminder: Enabled=" + std::to_string(airport.second.nap_reminder.enabled) + ", Hour=" + std::to_string(airport.second.nap_reminder.hour) + ", Minute=" + std::to_string(airport.second.nap_reminder.minute) + ", TZone=" + airport.second.nap_reminder.tzone, "Config");
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
