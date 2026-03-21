#pragma once
#include "CDelHelX_LookupsTools.h"

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

	/// @brief Toggles each timer tick; drives blinking tag colours.
	bool blinking = false;

	/// @brief Checks each configured airport's NAP reminder and fires a modal alert when the time is reached.
	/// @note Uses the airport's configured IANA timezone to evaluate the current local time.
	void CheckAirportNAPReminder();

	/// @brief Updates the TTT inbound flight-plan list: adds, updates distance, removes, and detects go-arounds.
	/// @note Also rebuilds the ttt_sortedByRunway index after each full pass.
	void UpdateTTTInbounds();

	/// @brief Updates the TWR same-SID outbound flight-plan list and records departure timing data.
	void UpdateTowerSameSID();

	/// @brief Updates the departure information overlays on the radar screen for all taxiing/departing aircraft.
	/// @note Only active when the logged-in controller's facility is GND (3) or above.
	void UpdateRadarTargetDepartureInfo();

	/// @brief Automatically detects which holding-point polygon a taxiing aircraft is in and writes it to slot 8.
	void AutoUpdateDepartureHoldingPoints();
};
