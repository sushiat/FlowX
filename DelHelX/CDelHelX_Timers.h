#pragma once
#include "CDelHelX_LookupsTools.h"
#include "reconnectSnapshot.h"
#include "tagInfo.h"
#include <future>
#include <map>
#include <set>
#include <string>

/// @brief Plugin layer that maintains per-aircraft state maps and drives periodic updates.
///
/// All state is stored in maps keyed by callsign (or callsign+runway designator for inbound tracking).
/// The Update* methods are called on a timer cadence from CDelHelX::OnTimer().
class CDelHelX_Timers : public CDelHelX_LookupsTools
{
protected:
    /// @brief Callsign -> tick-count (ms) at which the aircraft took off, or 0 while still on the ground.
    std::map<std::string, ULONGLONG> twrSameSID_flightPlans;

    /// @brief Runway designator -> callsign of the last departed aircraft from that runway.
    std::map<std::string, std::string> twrSameSID_lastDeparted;

    /// @brief Callsign -> WTC of the previous departure on the same runway (snapshot at this aircraft's takeoff).
    std::map<std::string, char> dep_prevWtc;

    /// @brief Callsign -> SID of the previous departure on the same runway.
    std::map<std::string, std::string> dep_prevSid;

    /// @brief Callsign -> elapsed seconds between the previous departure's takeoff and this aircraft's takeoff.
    std::map<std::string, ULONGLONG> dep_prevTakeoffOffset;

    /// @brief Callsign -> distance (NM) to the previous aircraft at the moment this aircraft took off.
    std::map<std::string, double> dep_prevDistanceAtTakeoff;

    /// @brief Callsign -> required time separation in seconds (computed from holding point relationship).
    std::map<std::string, int> dep_timeRequired;

    /// @brief Global sequence counter incremented at each takeoff.
    int dep_sequenceCounter = 0;

    /// @brief Callsign -> departure sequence number assigned at takeoff.
    std::map<std::string, int> dep_sequenceNumber;

    /// @brief Callsign -> cached content of flight-strip annotation slot 8.
    std::map<std::string, std::string> flightStripAnnotation;

    /// @brief Callsign+runway -> runway struct for aircraft currently in the TTT inbound list.
    std::map<std::string, runway> ttt_flightPlans;

    /// @brief Callsign+runway -> current distance (NM) to the runway threshold.
    std::map<std::string, double> ttt_distanceToRunway;

    /// @brief Runway designator -> sorted list of callsign+runway keys ordered by distance (nearest first).
    std::map<std::string, std::vector<std::string>> ttt_sortedByRunway;

    /// @brief Callsign+runway -> tick-count at which a go-around was detected.
    std::map<std::string, ULONGLONG> ttt_goAround;

    /// @brief Callsign+runway -> tick-count at which an aircraft was removed from the normal inbound list.
    std::map<std::string, ULONGLONG> ttt_recentlyRemoved;

    /// @brief Callsign -> assigned stand (populated from Ground Radar scratch-pad).
    std::map<std::string, std::string> standAssignment;

    /// @brief Callsign -> last known ground status string.
    std::map<std::string, std::string> groundStatus;

    /// @brief Callsign -> snapshot captured at disconnect, retained for up to 90 s for auto-restore on quick reconnect.
    std::map<std::string, reconnectSnapshot> reconnect_pending;

    /// @brief Toggles each timer tick; drives blinking tag colours.
    bool blinking = false;

    /// @brief ICAO -> current ATIS letter for each configured airport; empty string if not yet received.
    std::map<std::string, std::string> atisLetters;

    /// @brief Async future carrying the raw VATSIM data JSON; invalid when no fetch is in progress.
    std::future<std::string> atisFuture;

    /// @brief Last known QNH string per airport ICAO (e.g. "Q1013").
    std::map<std::string, std::string> airportQNH;

    /// @brief Last known wind string per airport ICAO (e.g. "27015KT").
    std::map<std::string, std::string> airportWind;

    /// @brief Airports where the wind value changed since the user last acknowledged.
    std::set<std::string> windUnacked;

    /// @brief Airports where the QNH value changed since the user last acknowledged.
    std::set<std::string> qnhUnacked;

    /// @brief Airports where the ATIS letter changed since the user last acknowledged.
    std::set<std::string> atisUnacked;

    /// @brief Formatted RVR display string per airport ICAO; empty if no RVR in the latest METAR.
    std::map<std::string, std::string> airportRVR;

    /// @brief Airports where the RVR changed since the user last acknowledged.
    std::set<std::string> rvrUnacked;

    /// @brief Callsign -> cached ADES tag (destination ICAO, or last IFR fix for type-Y plans).
    std::map<std::string, tagInfo> adesCache;

    /// @brief Checks each configured airport's NAP reminder and fires a modal alert when the time is reached.
    /// @note Uses the airport's configured IANA timezone to evaluate the current local time.
    void CheckAirportNAPReminder();

    /// @brief Updates the TTT inbound flight-plan list: adds, updates distance, removes, and detects go-arounds.
    /// @note Also rebuilds the ttt_sortedByRunway index after each full pass.
    void UpdateTTTInbounds();

    /// @brief Updates the TWR same-SID outbound flight-plan list and records departure timing data.
    void UpdateTowerSameSID();

    /// @brief Called on every radar position update for an aircraft; detects takeoff roll and airborne transition.
    /// Sets the roll tick in twrSameSID_flightPlans when GS ≥ 40 on the departure runway with matching heading.
    /// Assigns the departure sequence number and spacing data at the airborne moment (pressAlt ≥ fieldElev + 50 ft).
    void DetectTakeoffState(EuroScopePlugIn::CRadarTarget rt);

    /// @brief Updates the departure information overlays on the radar screen for all taxiing/departing aircraft.
    /// @note Only active when the logged-in controller's facility is GND (3) or above.
    void UpdateRadarTargetDepartureInfo();

    /// @brief Automatically detects which holding-point polygon a taxiing aircraft is in and writes it to slot 8.
    void AutoUpdateDepartureHoldingPoints();

    /// @brief Scans for reconnected pilots and restores their clearance flag and ground state if flight plan attributes match.
    /// @note Also expires snapshots that have been pending longer than 90 seconds.
    void CheckReconnects();

    /// @brief Checks whether any window positions have changed and persists them via SaveSettings if so.
    void SaveAndRestoreWindowLocations();

    /// @brief Fires a background VATSIM data fetch every 60 s and resolves the result into atisLetters.
    /// @param Counter The EuroScope timer counter passed from OnTimer.
    void PollAtisLetters(int Counter);

    /// @brief Rebuilds adesCache for all correlated flight plans.
    /// For type-Y plans returns the last IFR waypoint (turquoise); all others return the destination ICAO.
    void UpdateAdesCache();

public:
    /// @brief Clears all unacknowledged change flags for the given airport ICAO.
    /// @note Called from RadarScreen when the user clicks the WX/ATIS window row.
    void AckWeather(const std::string& icao);

public:
    /// @brief Records today's UTC date as the last NAP dismissal date and saves it to windowLocations.json.
    /// @note Called by RadarScreen when the user clicks the ACK button on the NAP reminder window.
    void AckNapReminder();
};
