/**
 * @file CFlowX_Settings.h
 * @brief Declaration of CFlowX_Settings, the configuration and persistence layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_Logging.h"
#include "config.h"
#include "nlohmann/json.hpp"
#include "osm_taxiways.h"
#include "taxi_graph.h"
#include <future>
#include <set>

using json = nlohmann::json;

/// @brief Plugin layer responsible for persisting settings and loading the airport configuration.
///
/// All user preferences and window positions are stored in settings.json in the plugin directory.
/// Airport configuration is loaded from config.json at startup.
class CFlowX_Settings : public CFlowX_Logging
{
  private:
    std::future<OsmResult> osmFuture; ///< Async future for an in-flight OSM taxiway fetch or cache load

  protected:
    std::set<std::string>          activeArrRunways;            ///< Runway designators currently active for arrivals (e.g. "34", "11"); refreshed by RefreshActiveRunways.
    std::set<std::string>          activeDepRunways;            ///< Runway designators currently active for departures (e.g. "16", "29"); refreshed by RefreshActiveRunways.
    std::map<std::string, double>  aircraftWingspans;           ///< Aircraft type ICAO → wingspan (m); missing entries filled with the per-WTC average at load time.
    std::map<std::string, airport> airports;                    ///< Airport configurations keyed by ICAO code
    bool                           apprEstColors       = false; ///< Whether the Approach Estimate window uses inbound-list colours (true) or always-green (false)
    bool                           approachEstVisible  = true;  ///< Whether the Approach Estimate window is visible; restored from settings.json
    int                            approachEstWindowH  = 380;   ///< Saved height of the Approach Estimate window; default 380
    int                            approachEstWindowW  = 260;   ///< Saved width of the Approach Estimate window; default 260
    int                            approachEstWindowX  = -1;    ///< Last-saved X position of the Approach Estimate window; -1 = not yet positioned
    int                            approachEstWindowY  = -1;    ///< Last-saved Y position of the Approach Estimate window; -1 = not yet positioned
    bool                           autoParked          = true;  ///< Whether arriving aircraft are automatically set to PARK when stopped at their assigned stand.
    bool                           autoScratchpadClear = false; ///< Whether scratchpad is automatically cleared on LINEUP/DEPA click for non-excluded content
    bool                           autoRestore         = false; ///< Whether quick-reconnect auto-restore of clearance flag and ground state is enabled
    bool                           hpAutoScratch       = true;  ///< Whether scratchpad HP shortcuts (.NAME / .NAME?) are active when logged in as TWR.
    int                            bgOpacity           = 100;   ///< Background opacity for custom windows in percent (20–100); title bar always opaque
    bool                           depRateVisible      = true;  ///< Whether the DEP/H departure rate window is visible; restored from settings.json
    int                            depRateWindowX      = -1;    ///< Last-saved X position of the departure rate window; -1 = not yet positioned
    int                            depRateWindowY      = -1;    ///< Last-saved Y position of the departure rate window; -1 = not yet positioned
    int                            fontOffset          = 0;     ///< Font size offset for all custom-window data fonts; positive = larger
    std::map<std::string, grStand> grStands;                    ///< Stand polygons keyed by "ICAO:StandName"; loaded from GRpluginStands.txt at startup.
    std::future<std::string>       latestVersion;               ///< Async future holding the fetched latest version string
    std::string                    napLastDismissedDate;        ///< UTC date (YYYY-MM-DD) on which the NAP reminder was last acknowledged
    int                            napWindowX         = -1;     ///< Last-saved X position of the NAP reminder window; -1 = not yet positioned
    int                            napWindowY         = -1;     ///< Last-saved Y position of the NAP reminder window; -1 = not yet positioned
    bool                           soundAirborne      = true;   ///< Whether the airborne audio alert is enabled
    bool                           soundGndTransfer   = true;   ///< Whether the GND transfer audio alert is enabled
    bool                           soundReadyTakeoff  = true;   ///< Whether the ready-for-takeoff audio alert is enabled
    bool                           soundTaxiConflict  = true;   ///< Whether the taxi conflict audio alert is enabled
    bool                           twrInboundVisible  = true;   ///< Whether the TWR Inbound window is visible; restored from settings.json
    int                            twrInboundWindowX  = -1;     ///< Last-saved X position of the TWR Inbound window; -1 = not yet positioned
    int                            twrInboundWindowY  = -1;     ///< Last-saved Y position of the TWR Inbound window; -1 = not yet positioned
    bool                           twrOutboundVisible = true;   ///< Whether the TWR Outbound window is visible; restored from settings.json
    int                            twrOutboundWindowX = -1;     ///< Last-saved X position of the TWR Outbound window; -1 = not yet positioned
    int                            twrOutboundWindowY = -1;     ///< Last-saved Y position of the TWR Outbound window; -1 = not yet positioned
    bool                           updateCheck        = true;   ///< Whether the background update check is enabled
    bool                           weatherVisible     = true;   ///< Whether the WX/ATIS window is visible; restored from settings.json
    int                            weatherWindowX     = -1;     ///< Last-saved X position of the WX/ATIS window; -1 = not yet positioned
    int                            weatherWindowY     = -1;     ///< Last-saved Y position of the WX/ATIS window; -1 = not yet positioned

    /// @brief Compares the fetched latest version against the running version and logs a message if outdated.
    /// @note Must only be called after the @c latestVersion future has become ready.
    void CheckForUpdate();

    /// @brief Loads ICAO_Aircraft.json from the adjacent Groundradar folder into @c aircraftWingspans.
    /// @note Aircraft without a wingspan are assigned the per-WTC average computed from the loaded data.
    void LoadAircraftData();

    /// @brief Loads GRpluginStands.txt from the adjacent Groundradar folder into @c grStands.
    /// @note Only STAND/COORD/BLOCKS lines are parsed; STANDLIST, GROUP, and other directives are ignored.
    void LoadGroundRadarStands();

    /// @brief Parses config.json from the plugin directory and populates the @c airports map.
    /// @note Logs a message and returns early if the file cannot be read or parsed.
    void LoadConfig();

    /// @brief Reads active runway designators from the EuroScope sector file and stores them in activeDepRunways / activeArrRunways.
    /// @note Call at startup and from OnAirportRunwayActivityChanged.
    void RefreshActiveRunways();

    /// @brief (Re)builds osmGraph from the current osmData and primary airport config.
    /// @note Called automatically by PollOsmFuture after each successful data load.
    void RebuildTaxiGraph();

    /// @brief Launches an async load of previously cached OSM taxiway data from disk.
    /// @note Called once at startup; result is consumed by PollOsmFuture.
    void StartOsmCacheLoad();

    /// @brief Polls osmFuture; on completion moves data into osmData, annotates holding-point ways, and logs the result.
    /// @note Called every timer tick from CFlowX::OnTimer.
    void PollOsmFuture();

    /// @brief Loads plugin settings (global toggles and window positions) from settings.json in the plugin directory.
    /// @note Missing or malformed file is silently ignored; positions stay at -1 (auto-place).
    void LoadSettings();

    /// @brief Writes plugin settings (global toggles and window positions) to settings.json in the plugin directory.
    void SaveSettings();

  public:
    OsmAirportData osmData;  ///< Last successfully loaded OSM taxiway/taxilane data; ways annotated with Taxiway_HoldingPoint on load
    TaxiGraph      osmGraph; ///< Taxiway routing graph; rebuilt after each successful OSM data load via RebuildTaxiGraph()

    /// @brief Constructs the settings layer, loading persisted settings and config.json on startup.
    CFlowX_Settings();

    /// @brief Returns the pre-built left/right runway label strings from config (e.g. {"16/34", "11/29"}).
    /// Always reflects the full configured set regardless of which runways have active inbounds.
    [[nodiscard]] std::pair<std::string, std::string> GetEstimateBarLabels() const
    {
        std::string left, right;
        for (const auto& [icao, apt] : this->airports)
        {
            for (const auto& [des, rwy] : apt.runways)
            {
                auto append = [](std::string& s, const std::string& v)
                { if (!s.empty()) { s += '/'; } s += v; };
                if (rwy.estimateBarSide == "left")
                {
                    append(left, des);
                }
                else if (rwy.estimateBarSide == "right")
                {
                    append(right, des);
                }
            }
        }
        return {left, right};
    }

    /// @brief Returns the estimateBarSide config value ("left"/"right") for the given runway designator; empty string if not found.
    [[nodiscard]] std::string GetRunwayEstimateBarSide(const std::string& designator) const
    {
        for (const auto& [icao, apt] : this->airports)
        {
            auto it = apt.runways.find(designator);
            if (it != apt.runways.end())
            {
                return it->second.estimateBarSide;
            }
        }
        return {};
    }

    /// @brief Returns a const reference to the active departure runway designator set.
    [[nodiscard]] const std::set<std::string>& GetActiveDepRunways() const
    {
        return this->activeDepRunways;
    }

    /// @brief Returns a const reference to the loaded airport configurations map.
    [[nodiscard]] const std::map<std::string, airport>& GetAirports() const
    {
        return this->airports;
    }

    /// @brief Returns a const reference to the GRplugin stand polygons map (keyed by "ICAO:StandName").
    [[nodiscard]] const std::map<std::string, grStand>& GetGrStands() const
    {
        return this->grStands;
    }

    /// @brief Returns whether Approach Estimate colors mode is enabled.
    [[nodiscard]] bool GetApprEstColors() const
    {
        return this->apprEstColors;
    }

    /// @brief Returns whether the Approach Estimate window is currently visible.
    [[nodiscard]] bool GetApproachEstVisible() const
    {
        return this->approachEstVisible;
    }

    /// @brief Returns the saved height of the Approach Estimate window in pixels; 0 = not yet saved.
    [[nodiscard]] int GetApproachEstH() const
    {
        return this->approachEstWindowH;
    }

    /// @brief Returns the saved width of the Approach Estimate window in pixels; 0 = not yet saved.
    [[nodiscard]] int GetApproachEstW() const
    {
        return this->approachEstWindowW;
    }

    /// @brief Returns the saved X position of the Approach Estimate window; -1 = not yet saved.
    [[nodiscard]] int GetApproachEstX() const
    {
        return this->approachEstWindowX;
    }

    /// @brief Returns the saved Y position of the Approach Estimate window; -1 = not yet saved.
    [[nodiscard]] int GetApproachEstY() const
    {
        return this->approachEstWindowY;
    }

    /// @brief Decreases the background opacity by 10 pp (floor 20%) and persists immediately.
    void DecreaseBgOpacity()
    {
        this->bgOpacity = std::max(20, this->bgOpacity - 10);
        SaveSettings();
    }

    /// @brief Decreases the font size offset by one step (floor −5) and persists immediately.
    void DecreaseFontOffset()
    {
        this->fontOffset = std::max(-5, this->fontOffset - 1);
        SaveSettings();
    }

    /// @brief Returns whether Auto Parked is enabled.
    [[nodiscard]] bool GetAutoParked() const
    {
        return this->autoParked;
    }

    /// @brief Returns the current auto-restore state.
    [[nodiscard]] bool GetAutoRestore() const
    {
        return this->autoRestore;
    }

    /// @brief Returns whether auto-clear scratchpad on lineup/takeoff click is enabled.
    [[nodiscard]] bool GetAutoScratchpadClear() const
    {
        return this->autoScratchpadClear;
    }

    /// @brief Returns whether the scratchpad HP shortcut feature is active.
    [[nodiscard]] bool GetHpAutoScratch() const
    {
        return this->hpAutoScratch;
    }

    /// @brief Returns the current background opacity in percent (20–100).
    [[nodiscard]] int GetBgOpacity() const
    {
        return this->bgOpacity;
    }

    /// @brief Returns whether the DEP/H departure rate window is currently visible.
    [[nodiscard]] bool GetDepRateVisible() const
    {
        return this->depRateVisible;
    }

    /// @brief Returns the current font size offset (positive = larger).
    [[nodiscard]] int GetFontOffset() const
    {
        return this->fontOffset;
    }

    /// @brief Returns true while an OSM taxiway fetch or cache load is in progress.
    [[nodiscard]] bool IsOsmBusy() const;

    /// @brief Launches a background Overpass API fetch for LOWW taxiway/taxilane data; no-op if already busy.
    void StartOsmFetch();

    /// @brief Returns whether the airborne audio alert is enabled.
    [[nodiscard]] bool GetSoundAirborne() const
    {
        return this->soundAirborne;
    }

    /// @brief Returns whether the GND transfer audio alert is enabled.
    [[nodiscard]] bool GetSoundGndTransfer() const
    {
        return this->soundGndTransfer;
    }

    /// @brief Returns whether the ready-for-takeoff audio alert is enabled.
    [[nodiscard]] bool GetSoundReadyTakeoff() const
    {
        return this->soundReadyTakeoff;
    }

    /// @brief Returns whether the taxi conflict audio alert is enabled.
    [[nodiscard]] bool GetSoundTaxiConflict() const
    {
        return this->soundTaxiConflict;
    }

    /// @brief Returns the wingspan in metres for @p acType, or 0 if unknown.
    [[nodiscard]] double GetAircraftWingspan(const std::string& acType) const
    {
        auto it = this->aircraftWingspans.find(acType);
        return it != this->aircraftWingspans.end() ? it->second : 0.0;
    }

    /// @brief Returns whether the TWR Inbound window is currently visible.
    [[nodiscard]] bool GetTwrInboundVisible() const
    {
        return this->twrInboundVisible;
    }

    /// @brief Returns whether the TWR Outbound window is currently visible.
    [[nodiscard]] bool GetTwrOutboundVisible() const
    {
        return this->twrOutboundVisible;
    }

    /// @brief Returns whether the WX/ATIS window is currently visible.
    [[nodiscard]] bool GetWeatherVisible() const
    {
        return this->weatherVisible;
    }

    /// @brief Returns whether flash-on-message is enabled.
    [[nodiscard]] bool GetFlashOnMessage() const
    {
        return this->flashOnMessage;
    }

    /// @brief Returns whether the update check is enabled.
    [[nodiscard]] bool GetUpdateCheck() const
    {
        return this->updateCheck;
    }

    /// @brief Increases the background opacity by 10 pp (ceiling 100%) and persists immediately.
    void IncreaseBgOpacity()
    {
        this->bgOpacity = std::min(100, this->bgOpacity + 10);
        SaveSettings();
    }

    /// @brief Increases the font size offset by one step (ceiling +5) and persists immediately.
    void IncreaseFontOffset()
    {
        this->fontOffset = std::min(5, this->fontOffset + 1);
        SaveSettings();
    }

    /// @brief Toggles the Approach Estimate colors setting and persists it immediately.
    void ToggleApprEstColors()
    {
        this->apprEstColors = !this->apprEstColors;
        SaveSettings();
    }

    /// @brief Toggles the Approach Estimate window visibility (does not affect the saved position).
    void ToggleApproachEstVisible()
    {
        this->approachEstVisible = !this->approachEstVisible;
    }

    /// @brief Toggles the Auto Parked feature and persists immediately.
    void ToggleAutoParked()
    {
        this->autoParked = !this->autoParked;
        SaveSettings();
    }

    /// @brief Toggles the auto-restore setting and persists it immediately.
    void ToggleAutoRestore()
    {
        this->autoRestore = !this->autoRestore;
        SaveSettings();
    }

    /// @brief Toggles the auto-clear scratchpad setting and persists it immediately.
    void ToggleAutoScratchpadClear()
    {
        this->autoScratchpadClear = !this->autoScratchpadClear;
        SaveSettings();
    }

    /// @brief Toggles the scratchpad HP shortcut feature and persists it immediately.
    void ToggleHpAutoScratch()
    {
        this->hpAutoScratch = !this->hpAutoScratch;
        SaveSettings();
    }

    /// @brief Toggles the debug-mode setting and persists it immediately.
    void ToggleDebug()
    {
        this->debug = !this->debug;
        SaveSettings();
        if (this->debug)
        {
            this->LogDebugSessionStart();
        }
    }

    /// @brief Toggles the flash-on-message setting and persists it immediately.
    void ToggleFlashOnMessage()
    {
        this->flashOnMessage = !this->flashOnMessage;
        SaveSettings();
    }

    /// @brief Toggles the airborne audio alert and persists immediately.
    void ToggleSoundAirborne()
    {
        this->soundAirborne = !this->soundAirborne;
        SaveSettings();
    }

    /// @brief Toggles the GND transfer audio alert and persists immediately.
    void ToggleSoundGndTransfer()
    {
        this->soundGndTransfer = !this->soundGndTransfer;
        SaveSettings();
    }

    /// @brief Toggles the ready-for-takeoff audio alert and persists immediately.
    void ToggleSoundReadyTakeoff()
    {
        this->soundReadyTakeoff = !this->soundReadyTakeoff;
        SaveSettings();
    }

    /// @brief Toggles the taxi conflict audio alert and persists immediately.
    void ToggleSoundTaxiConflict()
    {
        this->soundTaxiConflict = !this->soundTaxiConflict;
        SaveSettings();
    }

    /// @brief Toggles the update check setting and persists it immediately.
    void ToggleUpdateCheck()
    {
        this->updateCheck = !this->updateCheck;
        SaveSettings();
    }

    /// @brief Toggles the DEP/H departure rate window visibility (does not affect the saved position).
    void ToggleDepRateVisible()
    {
        this->depRateVisible = !this->depRateVisible;
    }

    /// @brief Toggles the TWR Inbound window visibility (does not affect the saved position).
    void ToggleTwrInboundVisible()
    {
        this->twrInboundVisible = !this->twrInboundVisible;
    }

    /// @brief Toggles the TWR Outbound window visibility (does not affect the saved position).
    void ToggleTwrOutboundVisible()
    {
        this->twrOutboundVisible = !this->twrOutboundVisible;
    }

    /// @brief Toggles the WX/ATIS window visibility (does not affect the saved position).
    void ToggleWeatherVisible()
    {
        this->weatherVisible = !this->weatherVisible;
    }
};
