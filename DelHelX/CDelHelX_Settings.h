#pragma once
#include "CDelHelX_Logging.h"
#include "config.h"
#include "nlohmann/json.hpp"
#include <future>

using json = nlohmann::json;

/// @brief Plugin layer responsible for persisting settings and loading the airport configuration.
///
/// Reads and writes plugin preferences via EuroScope's settings storage, parses config.json
/// into the @c airports map, and optionally checks for a newer plugin version in the background.
/// Window positions are stored separately in windowLocations.json in the plugin directory.
class CDelHelX_Settings : public CDelHelX_Logging
{
public:
    /// @brief Constructs the settings layer, loading persisted settings and config.json on startup.
    CDelHelX_Settings();

protected:
    bool updateCheck;                        ///< Whether the background update check is enabled

    bool autoRestore = false;               ///< Whether quick-reconnect auto-restore of clearance flag and ground state is enabled

    int depRateWindowX      = -1; ///< Last-saved X position of the departure rate window; -1 = not yet positioned
    int depRateWindowY      = -1; ///< Last-saved Y position of the departure rate window; -1 = not yet positioned
    int twrOutboundWindowX  = -1; ///< Last-saved X position of the TWR Outbound window; -1 = not yet positioned
    int twrOutboundWindowY  = -1; ///< Last-saved Y position of the TWR Outbound window; -1 = not yet positioned
    int twrInboundWindowX   = -1; ///< Last-saved X position of the TWR Inbound window; -1 = not yet positioned
    int twrInboundWindowY   = -1; ///< Last-saved Y position of the TWR Inbound window; -1 = not yet positioned
    int napWindowX          = -1; ///< Last-saved X position of the NAP reminder window; -1 = not yet positioned
    int napWindowY          = -1; ///< Last-saved Y position of the NAP reminder window; -1 = not yet positioned
    std::string napLastDismissedDate; ///< UTC date (YYYY-MM-DD) on which the NAP reminder was last acknowledged

    std::future<std::string> latestVersion;  ///< Async future holding the fetched latest version string

    std::map<std::string, airport> airports; ///< Airport configurations keyed by ICAO code

    /// @brief Loads persisted plugin settings from EuroScope's settings store.
    /// @note Reverts to defaults and saves them if the stored data is malformed.
    void LoadSettings();

    /// @brief Serialises current plugin settings and writes them to EuroScope's settings store.
    void SaveSettings();

    /// @brief Loads window positions from windowLocations.json in the plugin directory.
    /// @note Missing or malformed file is silently ignored; positions stay at -1 (auto-place).
    void LoadWindowLocations();

    /// @brief Writes current window positions to windowLocations.json in the plugin directory.
    void SaveWindowLocations();

    /// @brief Parses config.json from the plugin directory and populates the @c airports map.
    /// @note Logs a message and returns early if the file cannot be read or parsed.
    void LoadConfig();

    /// @brief Compares the fetched latest version against the running version and logs a message if outdated.
    /// @note Must only be called after the @c latestVersion future has become ready.
    void CheckForUpdate();
};
