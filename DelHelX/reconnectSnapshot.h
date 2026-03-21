#pragma once
#include <string>

/// @brief Snapshot of a disconnected flight plan, retained for up to 90 seconds to allow auto-restore on quick reconnect.
struct reconnectSnapshot
{
	std::string pilotName;         ///< Pilot name at disconnect
	std::string depAirport;        ///< Departure airport ICAO
	std::string destAirport;       ///< Destination airport ICAO
	std::string aircraftType;      ///< Aircraft type string (e.g. "B738")
	char wtc = ' ';                ///< Wake turbulence category
	double lat = 0.0;              ///< Last known latitude
	double lon = 0.0;              ///< Last known longitude
	bool hasPosition = false;      ///< True if a valid position was captured at disconnect
	std::string planType;          ///< Flight plan type string ("I" = IFR, "V" = VFR)
	std::string route;             ///< Filed route string
	std::string sidName;           ///< Filed SID name
	std::string squawk;            ///< Assigned squawk code
	bool clearanceFlag = false;    ///< Whether the clearance flag was set
	std::string savedGroundStatus; ///< Last tracked ground status from our own map
	ULONGLONG disconnectTime = 0;  ///< GetTickCount64() at the moment of disconnect
};
