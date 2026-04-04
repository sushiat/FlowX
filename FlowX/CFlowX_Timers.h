/**
 * @file CFlowX_Timers.h
 * @brief Declaration of CFlowX_Timers, the timer-driven aircraft state management layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_LookupsTools.h"
#include "reconnectSnapshot.h"
#include "tagInfo.h"
#include <future>
#include <map>
#include <set>
#include <string>

/// @brief Plugin layer that maintains per-aircraft state maps and drives periodic updates.
///
/// All state is stored in maps keyed by callsign (or callsign+runway designator for inbound tracking).
/// The Update* methods are called on a timer cadence from CFlowX::OnTimer().
class CFlowX_Timers : public CFlowX_LookupsTools
{
  protected:
    std::map<std::string, tagInfo>                  adesCache;                 ///< Callsign -> cached ADES tag (destination ICAO, or last IFR fix for type-Y plans).
    std::map<std::string, std::string>              airportQNH;                ///< Last known QNH string per airport ICAO (e.g. "Q1013").
    std::map<std::string, std::string>              airportRVR;                ///< Formatted RVR display string per airport ICAO; empty if no RVR in the latest METAR.
    std::map<std::string, std::string>              airportWind;               ///< Last known wind string per airport ICAO (e.g. "27015KT").
    std::future<std::string>                        atisFuture;                ///< Async future carrying the raw VATSIM data JSON; invalid when no fetch is in progress.
    std::map<std::string, std::string>              atisLetters;               ///< ICAO -> current ATIS letter for each configured airport; empty string if not yet received.
    std::set<std::string>                           atisUnacked;               ///< Airports where the ATIS letter changed since the user last acknowledged.
    bool                                            blinking = false;          ///< Toggles each timer tick; drives blinking tag colours.
    std::map<std::string, std::string>              dep_prevCallSign;          ///< Callsign -> callsign of the previous departure on the same runway (captured at airborne moment).
    std::map<std::string, double>                   dep_prevDistanceAtTakeoff; ///< Callsign -> distance (NM) to the previous aircraft at the moment this aircraft took off.
    std::map<std::string, std::string>              dep_prevSid;               ///< Callsign -> SID of the previous departure on the same runway.
    std::map<std::string, ULONGLONG>                dep_prevTakeoffOffset;     ///< Callsign -> elapsed seconds between the previous departure's takeoff and this aircraft's takeoff.
    std::map<std::string, char>                     dep_prevWtc;               ///< Callsign -> WTC of the previous departure on the same runway (snapshot at this aircraft's takeoff).
    int                                             dep_sequenceCounter = 0;   ///< Global sequence counter incremented at each takeoff.
    std::map<std::string, int>                      dep_sequenceNumber;        ///< Callsign -> departure sequence number assigned at takeoff.
    std::map<std::string, int>                      dep_timeRequired;          ///< Callsign -> required time separation in seconds (computed from holding point relationship).
    std::map<std::string, std::string>              flightStripAnnotation;     ///< Callsign -> cached content of flight-strip annotation slot 8.
    std::map<std::string, std::string>              groundStatus;              ///< Callsign -> last known ground status string.
    std::set<std::string>                           qnhUnacked;                ///< Airports where the QNH value changed since the user last acknowledged.
    std::map<std::string, reconnectSnapshot>        reconnect_pending;         ///< Callsign -> snapshot captured at disconnect, retained for up to 90 s for auto-restore on quick reconnect.
    std::set<std::string>                           rvrUnacked;                ///< Airports where the RVR changed since the user last acknowledged.
    std::map<std::string, std::string>              standAssignment;           ///< Callsign -> assigned stand (populated from Ground Radar scratch-pad).
    std::set<std::string>                           ttt_clearedToLand;         ///< Callsigns for which cleared-to-land has been issued; erased on go-around, removal, or disconnect.
    std::map<std::string, double>                   ttt_distanceToRunway;      ///< Callsign+runway -> current distance (NM) to the runway threshold.
    std::map<std::string, runway>                   ttt_flightPlans;           ///< Callsign+runway -> runway struct for aircraft currently in the TTT inbound list.
    std::map<std::string, ULONGLONG>                ttt_goAround;              ///< Callsign+runway -> tick-count at which a go-around was detected.
    std::map<std::string, ULONGLONG>                ttt_recentlyRemoved;       ///< Callsign+runway -> tick-count at which an aircraft was removed from the normal inbound list.
    std::map<std::string, std::vector<std::string>> ttt_sortedByRunway;        ///< Runway designator -> sorted list of callsign+runway keys ordered by distance (nearest first).
    std::map<std::string, ULONGLONG>                twrSameSID_flightPlans;    ///< Callsign -> tick-count (ms) at which the aircraft took off, or 0 while still on the ground.
    std::map<std::string, std::string>              twrSameSID_lastDeparted;   ///< Runway designator -> callsign of the last departed aircraft from that runway.
    std::set<std::string>                           windUnacked;               ///< Airports where the wind value changed since the user last acknowledged.

    /// @brief Automatically detects which holding-point polygon a taxiing aircraft is in and writes it to slot 8.
    void AutoUpdateDepartureHoldingPoints();

    /// @brief Checks each configured airport's NAP reminder and fires a modal alert when the time is reached.
    /// @note Uses the airport's configured IANA timezone to evaluate the current local time.
    void CheckAirportNAPReminder();

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

    /// @brief Restores persisted window positions to screens that have not yet been positioned (pos == -1).
    /// @note Position saving is now triggered explicitly via SaveWindowPositions().
    void SaveAndRestoreWindowLocations();

    /// @brief Rebuilds adesCache for all correlated flight plans.
    /// For type-Y plans returns the last IFR waypoint (turquoise); all others return the destination ICAO.
    void UpdateAdesCache();

    /// @brief Updates the departure information overlays on the radar screen for all taxiing/departing aircraft.
    /// @note Only active when the logged-in controller's facility is GND (3) or above.
    void UpdateRadarTargetDepartureInfo();

    /// @brief Updates the TWR Outbound flight-plan list: adds/removes aircraft, detects takeoff and airborne transitions, and manages departure spacing state.
    void UpdateTWROutbound();

    /// @brief Updates the TTT inbound flight-plan list: adds, updates distance, removes, and detects go-arounds.
    /// @note Also rebuilds the ttt_sortedByRunway index after each full pass.
    void UpdateTWRInbound();

  public:
    /// @brief Records today's UTC date as the last NAP dismissal date and saves it to windowLocations.json.
    /// @note Called by RadarScreen when the user clicks the ACK button on the NAP reminder window.
    void AckNapReminder();

    /// @brief Syncs current on-screen window positions into the settings layer and writes windowLocations.json.
    /// @note Called when the user clicks "Save positions" in the FlowX menu.
    void SaveWindowPositions();

    /// @brief Clears all unacknowledged change flags for the given airport ICAO.
    /// @note Called from RadarScreen when the user clicks the WX/ATIS window row.
    void AckWeather(const std::string& icao);
};
