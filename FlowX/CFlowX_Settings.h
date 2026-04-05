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
#include <future>

using json = nlohmann::json;

/// @brief Plugin layer responsible for persisting settings and loading the airport configuration.
///
/// Reads and writes plugin preferences via EuroScope's settings storage, parses config.json
/// into the @c airports map, and optionally checks for a newer plugin version in the background.
/// Window positions and global UI settings are stored separately in windowSettings.json in the plugin directory.
class CFlowX_Settings : public CFlowX_Logging
{
  protected:
    std::map<std::string, double>  aircraftWingspans;       ///< Aircraft type ICAO → wingspan (m); missing entries filled with the per-WTC average at load time.
    std::map<std::string, airport> airports;                ///< Airport configurations keyed by ICAO code
    bool                           autoRestore    = false;  ///< Whether quick-reconnect auto-restore of clearance flag and ground state is enabled
    bool                           depRateVisible  = true;  ///< Whether the DEP/H departure rate window is visible; restored from windowSettings.json
    int                            depRateWindowX = -1;     ///< Last-saved X position of the departure rate window; -1 = not yet positioned
    int                            depRateWindowY = -1;     ///< Last-saved Y position of the departure rate window; -1 = not yet positioned
    int                            fontOffset         = 0;  ///< Font size offset for all custom-window data fonts; positive = larger
    std::map<std::string, grStand> grStands;                ///< Stand polygons keyed by "ICAO:StandName"; loaded from GRpluginStands.txt at startup.
    std::future<std::string>       latestVersion;           ///< Async future holding the fetched latest version string
    std::string                    napLastDismissedDate;    ///< UTC date (YYYY-MM-DD) on which the NAP reminder was last acknowledged
    int                            napWindowX         = -1; ///< Last-saved X position of the NAP reminder window; -1 = not yet positioned
    int                            napWindowY         = -1; ///< Last-saved Y position of the NAP reminder window; -1 = not yet positioned
    bool                           twrInboundVisible  = true;  ///< Whether the TWR Inbound window is visible; restored from windowSettings.json
    int                            twrInboundWindowX  = -1; ///< Last-saved X position of the TWR Inbound window; -1 = not yet positioned
    int                            twrInboundWindowY  = -1; ///< Last-saved Y position of the TWR Inbound window; -1 = not yet positioned
    bool                           twrOutboundVisible = true;  ///< Whether the TWR Outbound window is visible; restored from windowSettings.json
    int                            twrOutboundWindowX = -1; ///< Last-saved X position of the TWR Outbound window; -1 = not yet positioned
    int                            twrOutboundWindowY = -1; ///< Last-saved Y position of the TWR Outbound window; -1 = not yet positioned
    bool                           updateCheck;             ///< Whether the background update check is enabled
    bool                           weatherVisible     = true;  ///< Whether the WX/ATIS window is visible; restored from windowSettings.json
    int                            weatherWindowX = -1;     ///< Last-saved X position of the WX/ATIS window; -1 = not yet positioned
    int                            weatherWindowY = -1;     ///< Last-saved Y position of the WX/ATIS window; -1 = not yet positioned

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

    /// @brief Loads persisted plugin settings from EuroScope's settings store.
    /// @note Reverts to defaults and saves them if the stored data is malformed.
    void LoadSettings();

    /// @brief Loads window positions and global UI settings from windowSettings.json in the plugin directory.
    /// @note Missing or malformed file is silently ignored; positions stay at -1 (auto-place).
    void LoadWindowSettings();

    /// @brief Serialises current plugin settings and writes them to EuroScope's settings store.
    void SaveSettings();

    /// @brief Writes window positions and global UI settings to windowSettings.json in the plugin directory.
    void SaveWindowSettings();

  public:
    /// @brief Constructs the settings layer, loading persisted settings and config.json on startup.
    CFlowX_Settings();

    /// @brief Decreases the font size offset by one step (floor −5) and persists immediately.
    void DecreaseFontOffset() { this->fontOffset = std::max(-5, this->fontOffset - 1); SaveWindowSettings(); }

    /// @brief Returns the current auto-restore state.
    [[nodiscard]] bool GetAutoRestore() const { return this->autoRestore; }

    /// @brief Returns whether the DEP/H departure rate window is currently visible.
    [[nodiscard]] bool GetDepRateVisible() const { return this->depRateVisible; }

    /// @brief Returns the current font size offset (positive = larger).
    [[nodiscard]] int GetFontOffset() const { return this->fontOffset; }

    /// @brief Returns whether the TWR Inbound window is currently visible.
    [[nodiscard]] bool GetTwrInboundVisible() const { return this->twrInboundVisible; }

    /// @brief Returns whether the TWR Outbound window is currently visible.
    [[nodiscard]] bool GetTwrOutboundVisible() const { return this->twrOutboundVisible; }

    /// @brief Returns whether the WX/ATIS window is currently visible.
    [[nodiscard]] bool GetWeatherVisible() const { return this->weatherVisible; }

    /// @brief Increases the font size offset by one step (ceiling +5) and persists immediately.
    void IncreaseFontOffset() { this->fontOffset = std::min(5, this->fontOffset + 1); SaveWindowSettings(); }

    /// @brief Toggles the auto-restore setting and persists it immediately.
    void ToggleAutoRestore() { this->autoRestore = !this->autoRestore; SaveSettings(); }

    /// @brief Toggles the debug-mode setting and persists it immediately.
    void ToggleDebug() { this->debug = !this->debug; SaveSettings(); }

    /// @brief Toggles the DEP/H departure rate window visibility (does not affect the saved position).
    void ToggleDepRateVisible() { this->depRateVisible = !this->depRateVisible; }

    /// @brief Toggles the TWR Inbound window visibility (does not affect the saved position).
    void ToggleTwrInboundVisible() { this->twrInboundVisible = !this->twrInboundVisible; }

    /// @brief Toggles the TWR Outbound window visibility (does not affect the saved position).
    void ToggleTwrOutboundVisible() { this->twrOutboundVisible = !this->twrOutboundVisible; }

    /// @brief Toggles the WX/ATIS window visibility (does not affect the saved position).
    void ToggleWeatherVisible() { this->weatherVisible = !this->weatherVisible; }
};
