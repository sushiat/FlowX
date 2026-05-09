/**
 * @file CFlowX_Timers.h
 * @brief Declaration of CFlowX_Timers, the timer-driven aircraft state management layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_LookupsTools.h"
#include "osm_taxiways.h"
#include "reconnectSnapshot.h"
#include "tagInfo.h"
#include <deque>
#include <future>
#include <map>
#include <set>
#include <string>
#include <vector>

/// @brief Spacing data captured at takeoff (prevCallSign) and TOC (remaining fields) for an outbound aircraft.
/// Populated in two stages: prevCallSign is set at the airborne moment; snapshotTaken + all other fields
/// are set by RecordDepartureSpacingSnapshot at transfer-of-communication.
struct DepartureLiveSpacing
{
    std::string prevCallSign;              ///< Callsign of the previous departure on the same runway.
    std::string prevSid;                   ///< SID of the previous departure.
    char        prevWtc           = '\0';  ///< WTC of the previous departure.
    ULONGLONG   takeoffTimeOffset = 0;     ///< Elapsed seconds between the previous takeoff and this one.
    double      distanceAtTakeoff = 0.0;   ///< NM between this aircraft and the previous one at liftoff.
    int         timeRequired      = 0;     ///< Required separation in seconds (holding-point adjusted).
    bool        snapshotTaken     = false; ///< True once RecordDepartureSpacingSnapshot has completed.
};

/// @brief Per-aircraft TTT inbound tracking state. Keyed by plain callsign in ttt_inbound.
/// The runway designator is stored in flightPlan.designator; ttt_recentlyRemoved still uses
/// the compound callsign+runway key for per-runway anti-bounce semantics.
struct TTTInboundState
{
    runway      flightPlan;                 ///< Runway struct (designator, thresholds, approach paths, vacate points, etc.).
    double      distanceToRunway = 0.0;     ///< NM to runway threshold; updated every position report.
    ULONGLONG   frozenTick       = 0;       ///< Non-zero while in 30-second frozen-exit display; set to GetTickCount64() at cone exit.
    ULONGLONG   goAroundTick     = 0;       ///< 0 = no go-around detected; non-zero = tick at detection.
    std::string frozenTttStr;               ///< TTT string captured at freeze time (e.g. "02:12"); displayed as "?02:12?".
    bool        approachFixTracked = false; ///< True while tracking a non-straight-in RNP approach leg.
    bool        goAroundConfirmed  = false; ///< True once the controller right-clicked to confirm the go-around; suppresses the 60-second auto-removal timeout.
    bool        wasTrackedByMe     = false; ///< True if GetTrackingControllerIsMe() was true at go-around detection; gates tag-drop and handoff removal.
    int         approachPathIdx    = -1;    ///< Index into flightPlan.gpsApproachPaths; -1 if unused.
    int         approachSegIdx     = -1;    ///< Index of the last passed fix in the approach path; -1 if unused.
};

/// @brief A pending ground-status push in the redo-flags background drain.
struct RedoFlagTask
{
    std::string callSign;
    std::string groundState;
};

/// @brief Plugin layer that maintains per-aircraft state maps and drives periodic updates.
///
/// All state is stored in maps keyed by callsign.
/// The Update* methods are called on a timer cadence from CFlowX::OnTimer().
class CFlowX_Timers : public CFlowX_LookupsTools
{
  protected:
    /// @brief Plain-data snapshot of one slow/grounded radar target used for off-thread stand containment checks.
    /// Contains no EuroScope handles — safe to move into a worker-thread lambda.
    struct StandCheckTarget
    {
        std::string callSign; ///< Aircraft callsign.
        double      lat;      ///< Position latitude.
        double      lon;      ///< Position longitude.
        int         pressAlt; ///< Pressure altitude in feet.
        double      wingspan; ///< Resolved wingspan in metres; 0.0 if unknown.
    };

    std::map<std::string, tagInfo>                    adesCache;                        ///< Callsign -> cached ADES tag (destination ICAO, or last IFR fix for type-Y plans).
    std::map<std::string, std::string>                airportQNH;                       ///< Last known QNH string per airport ICAO (e.g. "Q1013").
    std::map<std::string, std::string>                airportRVR;                       ///< Formatted RVR display string per airport ICAO; empty if no RVR in the latest METAR.
    std::map<std::string, std::string>                airportWind;                      ///< Last known wind string per airport ICAO (e.g. "27015KT").
    std::future<std::map<std::string, std::string>>   atisFuture;                       ///< Worker-thread pre-parsed ATIS result (ICAO → atis_code); invalid when no fetch is in progress.
    std::map<std::string, std::string>                atisLetters;                      ///< ICAO -> current ATIS letter for each configured airport; empty string if not yet received.
    std::set<std::string>                             atisUnacked;                      ///< Airports where the ATIS letter changed since the user last acknowledged.
    bool                                              blinking                 = false; ///< Toggles each timer tick; drives blinking tag colours.
    bool                                              connectedRunwayRefreshed = false; ///< True once RefreshActiveRunways has fired for the current connection.
    int                                               connectedType            = 0;     ///< Connection type at the previous timer tick; used to detect connect/disconnect transitions.
    ULONGLONG                                         connectedTickMs          = 0;     ///< GetTickCount64() value when the last connect transition was observed; 0 = not yet connected.
    int                                               dbg_positionCalls        = 0;     ///< Total OnRadarTargetPositionUpdate calls fired.
    int                                               dbg_positionInbound      = 0;     ///< Subset where the aircraft was a confirmed inbound.
    int                                               dbg_positionOutbound     = 0;     ///< Subset where the aircraft was a tracked outbound (live spacing active).
    int                                               dbg_standLaunches        = 0;     ///< Times UpdateOccupiedStands launched a worker thread.
    int                                               dbg_standSkips           = 0;     ///< Times UpdateOccupiedStands skipped launch (worker still running).
    int                                               dbg_tagItemCalls         = 0;     ///< Total OnGetTagItem calls.
    int                                               dbg_timerTicks           = 0;     ///< Total OnTimer calls.
    std::map<std::string, std::deque<GeoPoint>>       gndTailHistory;                   ///< Callsign -> per-second position history used to draw tail dots behind each aircraft; grows when moving, drains when stopped.
    std::map<std::string, DepartureLiveSpacing>       dep_liveSpacing;                  ///< Callsign -> spacing data for the current departure; populated in two stages (see DepartureLiveSpacing).
    std::map<std::string, int>                        dep_queuePos;                     ///< Callsign -> departure queue position (1-based); absent = not queued.
    int                                               dep_sequenceCounter = 0;          ///< Global sequence counter incremented at each takeoff.
    std::map<std::string, int>                        dep_sequenceNumber;               ///< Callsign -> departure sequence number assigned at takeoff.
    std::map<std::string, std::string>                departureStand;                   ///< Callsign -> last stand an outbound was detected parked in (polygon-derived; display-only for DIFLIS, never cleared on leaving).
    std::map<std::string, std::string>                flightStripAnnotation;            ///< Callsign -> cached content of flight-strip annotation slot 8.
    std::map<std::string, std::string>                lastDepRunway;                    ///< Callsign -> last seen departure runway designator; used to detect runway changes and clear stale HP assignments.
    std::map<std::string, EuroScopePlugIn::CPosition> lastHpCheckPos;                   ///< Callsign -> aircraft position at the last holding-point polygon test; used to skip redundant checks.
    std::set<std::string>                             gndTransfer_list;                 ///< Callsigns of landed inbounds awaiting GND handoff (added at TTT removal, cleared on click/disconnect).
    std::set<std::string>                             gndTransfer_soundPlayed;          ///< Subset of gndTransfer_list where GS<50 was first detected; square is shown and sound has played.
    std::map<std::string, std::string>                groundStatus;                     ///< Callsign -> last known ground status string.
    std::set<std::string>                             qnhUnacked;                       ///< Airports where the QNH value changed since the user last acknowledged.
    std::map<std::string, ULONGLONG>                  readyTakeoff_okTick;              ///< Tick when depInfo first became "OK" for a lined-up aircraft; used for the 5 s takeoff-ready reminder.
    std::set<std::string>                             readyTakeoff_soundPlayed;         ///< Callsigns where the takeoff-ready sound has already fired for the current "OK" episode; reset when depInfo or state changes.
    std::set<std::string>                             readyTakeoff_wasWaiting;          ///< Callsigns that had a non-"OK" depInfo while in LINEUP; gate that prevents firing when LINEUP was given with depInfo already "OK".
    std::map<std::string, reconnectSnapshot>          reconnect_pending;                ///< Callsign -> snapshot captured at disconnect, retained for up to 90 s for auto-restore on quick reconnect.
    std::vector<RedoFlagTask>                         redoFlagQueue;                    ///< Tasks queued by RedoFlags() for main-thread dispatch via DrainRedoFlagQueue().
    std::set<std::string>                             rvrUnacked;                       ///< Airports where the RVR changed since the user last acknowledged.
    std::map<std::string, std::string>                standAssignment;                  ///< Callsign -> assigned stand (populated from Ground Radar scratch-pad).
    std::map<std::string, std::string>                standOccupancy;                   ///< Stand name → callsign of the occupying or blocking aircraft; updated from stand-occupancy worker thread.
    std::future<std::map<std::string, std::string>>   standOccupancyFuture;             ///< Worker-thread stand-occupancy result; invalid when no computation is in progress.
    std::map<std::string, TTTInboundState>            ttt_inbound;                      ///< Callsign -> TTT inbound state; plain callsign key (one active entry per aircraft).
    std::set<std::string>                             ttt_callSigns;                    ///< Callsigns currently present in ttt_inbound; O(log N) membership test used in hot paths.
    std::set<std::string>                             ttt_clearedToLand;                ///< Callsigns for which cleared-to-land has been issued; erased on go-around, removal, or disconnect.
    std::map<std::string, ULONGLONG>                  ttt_recentlyRemoved;              ///< Callsign+runway compound key -> tick at removal; per-runway anti-bounce (prevents re-entry within 60 s).
    std::set<std::string>                             ttt_runwayOccupied;               ///< Runway designators that currently have a ground radar target within their runway bounds; refreshed each timer tick.
    std::map<std::string, std::vector<std::string>>   ttt_sortedByRunway;               ///< Runway designator -> sorted list of callsigns ordered by distance to threshold (nearest first).
    std::map<std::string, ULONGLONG>                  twrSameSID_flightPlans;           ///< Callsign -> tick-count (ms) at which the aircraft took off, or 0 while still on the ground.
    std::map<std::string, std::string>                twrSameSID_lastDeparted;          ///< Runway designator -> callsign of the last departed aircraft from that runway.
    std::set<std::string>                             windUnacked;                      ///< Airports where the wind value changed since the user last acknowledged.

    /// @brief Checks each configured airport's NAP reminder and fires a modal alert when the time is reached.
    /// @note Uses the airport's configured IANA timezone to evaluate the current local time.
    void CheckAirportNAPReminder();

    /// @brief Detects arriving aircraft that have stopped inside their assigned stand polygon and sets ground status to PARK.
    /// Fires once per aircraft; idempotent due to the arrivedAtStand guard.
    void CheckArrivedAtStand();

    /// @brief Scans for reconnected pilots and restores their clearance flag and ground state if flight plan attributes match.
    /// @note Also expires snapshots that have been pending longer than 90 seconds.
    void CheckReconnects();

    /// @brief Called on every radar position update for an aircraft; detects takeoff roll and airborne transition.
    /// Sets the roll tick in twrSameSID_flightPlans when GS ≥ 40 on the departure runway with matching heading.
    /// Assigns the departure sequence number at the airborne moment (pressAlt ≥ fieldElev + 50 ft).
    void DetectTakeoffState(EuroScopePlugIn::CRadarTarget rt);

    /// @brief Snapshots spacing data (distance, WTC, SID, time offset) for the given departing aircraft.
    /// Intended to be called at transfer-of-communication time rather than at liftoff.
    /// Idempotent: does nothing if the snapshot has already been recorded.
    void RecordDepartureSpacingSnapshot(const std::string& callSign);

    /// @brief Fires a background VATSIM data fetch every 60 s and resolves the result into atisLetters.
    /// @param Counter The EuroScope timer counter passed from OnTimer.
    void PollAtisLetters(int Counter);

    /// @brief Rebuilds adesCache for all correlated flight plans.
    /// For type-Y plans returns the last IFR waypoint (turquoise); all others return the destination ICAO.
    void UpdateAdesCache();

    /// @brief Rebuilds standOccupancy by checking all slow/grounded radar targets against the loaded stand polygons.
    /// Blocks with a wingspan threshold are applied only when the occupying aircraft's wingspan meets the minimum.
    void UpdateOccupiedStands();

    /// @brief Updates the departure information overlays on the radar screen for all taxiing/departing aircraft.
    /// @note Only active when the logged-in controller's facility is GND (3) or above.
    void UpdateRadarTargetDepartureInfo();

    /// @brief Updates the TWR Outbound flight-plan list: adds/removes aircraft, detects takeoff and airborne transitions, and manages departure spacing state.
    void UpdateTWROutbound();

    /// @brief Updates the TTT inbound flight-plan list: adds, updates distance, removes, and detects go-arounds.
    /// @note Also rebuilds the ttt_sortedByRunway index after each full pass.
    void UpdateTWRInbound();

    /// @brief Samples every correlated radar target once per second and maintains the per-aircraft tail history.
    /// Moved (> ~3 m) → push_back current position and trim to the configured dot count.
    /// Not moved → pop_front one sample, so a stopped aircraft's trail drains one dot per tick.
    void UpdateGndTailHistory();

  public:
    /// @brief Records today's UTC date as the last NAP dismissal date and persists settings.
    /// @note Called by RadarScreen when the user clicks the ACK button on the NAP reminder window.
    void AckNapReminder();

    /// @brief Returns the current ATIS letter for the given ICAO, or an empty string if none known.
    [[nodiscard]] std::string GetAtisLetter(const std::string& icao) const
    {
        auto it = this->atisLetters.find(icao);
        return (it != this->atisLetters.end()) ? it->second : std::string{};
    }

    /// @brief Returns the raw METAR pressure element for the given ICAO (e.g. "Q1013" / "A2992"),
    ///        or an empty string if no METAR has been parsed yet.
    [[nodiscard]] std::string GetAirportQnh(const std::string& icao) const
    {
        auto it = this->airportQNH.find(icao);
        return (it != this->airportQNH.end()) ? it->second : std::string{};
    }

    /// @brief Syncs current on-screen window positions into the settings layer and persists them.
    /// @note Called when the user clicks "Save positions" in the FlowX menu.
    void SaveWindowPositions();

    /// @brief Clears all unacknowledged change flags for the given airport ICAO.
    /// @note Called from RadarScreen when the user clicks the WX/ATIS window row.
    void AckWeather(const std::string& icao);

    /// @brief Removes a callsign from the GND-transfer tracking state (list + soundPlayed).
    /// @note Called from RadarScreen when a taxi route is assigned to an inbound aircraft.
    void ClearGndTransfer(const std::string& callsign);

    /// @brief Returns the callsign→stand assignment map (populated from Ground Radar scratch-pad).
    [[nodiscard]] const std::map<std::string, std::string>& GetStandAssignment() const
    {
        return this->standAssignment;
    }

    /// @brief Returns the per-aircraft tail-dot history map (callsign → ring buffer of recent positions).
    [[nodiscard]] const std::map<std::string, std::deque<GeoPoint>>& GetGndTailHistory() const
    {
        return this->gndTailHistory;
    }

    /// @brief Returns the callsign→ground status map (plugin-specific states: ONFREQ, ST-UP, PUSH, TAXI, …).
    [[nodiscard]] const std::map<std::string, std::string>& GetGroundStatus() const
    {
        return this->groundStatus;
    }

    /// @brief Returns the arrival runway designator for an inbound aircraft, or empty if not tracked.
    [[nodiscard]] std::string GetArrivalRunway(const std::string& callsign) const
    {
        auto it = this->ttt_inbound.find(callsign);
        return (it != this->ttt_inbound.end()) ? it->second.flightPlan.designator : std::string{};
    }

    /// @brief Writes a holding-point name into the flight-strip annotation and syncs to other controllers.
    /// @note Called from RadarScreen when a taxi route ending at a runway HP is accepted.
    void AssignHoldingPoint(EuroScopePlugIn::CFlightPlan& fp, const std::string& hpName);
};
