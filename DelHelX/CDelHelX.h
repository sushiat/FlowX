#pragma once

#include <string>
#include <filesystem>

#include "CDelHelX_Functions.h"
#include "EuroScope/EuroScopePlugIn.h"

#include "config.h"

using namespace std::chrono_literals;

/// @brief Top-level plugin class; dispatches all EuroScope events to the appropriate layer.
///
/// Handles chat commands, tag item rendering, function callbacks, periodic timer ticks,
/// METAR processing, and flight plan lifecycle events.
class CDelHelX : public CDelHelX_Functions
{
public:
    /// @brief Constructs the main plugin object and initialises override flags to false.
    CDelHelX();

    /// @brief Processes `.delhelx` chat commands entered in the EuroScope message area.
    /// @param sCommandLine Full command line string entered by the user.
    /// @return True if the command was recognised and handled; false otherwise.
    bool OnCompileCommand(const char* sCommandLine) override;

    /// @brief Called by EuroScope to retrieve tag item text and colour for a given item code.
    /// @param FlightPlan Flight plan associated with the tag row.
    /// @param RadarTarget Correlated radar target.
    /// @param ItemCode Tag item type identifier (TAG_ITEM_* constant).
    /// @param TagData Additional tag data (unused by this plugin).
    /// @param sItemString Output buffer (max 15 chars + NUL) for the tag text.
    /// @param pColorCode Output for the EuroScope colour mode flag.
    /// @param pRGB Output for the RGB colour when pColorCode indicates RGB mode.
    /// @param pFontSize Output for the optional font size override.
    void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) override;

    /// @brief Called by EuroScope when a controller clicks a tag function button.
    /// @param FunctionId Tag function identifier (TAG_FUNC_* constant).
    /// @param sItemString Current text of the clicked tag cell.
    /// @param Pt Screen position of the click.
    /// @param Area Bounding rectangle of the tag cell.
    void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area) override;

    /// @brief Called by EuroScope on a regular interval; drives blinking, update checks, and state updates.
    /// @param Counter Monotonically increasing tick counter supplied by EuroScope.
    void OnTimer(int Counter) override;

    /// @brief Called when a new METAR is received; detects QNH changes and flags affected aircraft.
    /// @param sStation ICAO station identifier.
    /// @param sFullMetar Full METAR string.
    void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override;

    /// @brief Called when a flight plan disconnects; removes the aircraft from all state maps.
    /// @param FlightPlan The disconnecting flight plan.
    void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;

    /// @brief Overrides base screen creation to restore persisted window positions before the first draw.
    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

    /// @brief Called when controller-assigned data changes; tracks ground status and stand assignments.
    /// @param fp Updated flight plan.
    /// @param dataType EuroScope data-type constant indicating which field changed.
    void OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType) override;

private:
    /// @brief Last known QNH string per airport ICAO (e.g. "Q1013").
    std::map<std::string, std::string> airportQNH;

    /// @brief Last full METAR string received per airport ICAO, used to suppress duplicate log output.
    std::map<std::string, std::string> lastMetar;

    /// @brief Re-evaluates and re-sets the EuroScope clearance flag for all ground-based cleared aircraft.
    /// @note Used to recover from flag corruption; operates on untracked and self-tracked aircraft only.
    void RedoFlags();
};
