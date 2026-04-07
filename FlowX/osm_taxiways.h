/**
 * @file osm_taxiways.h
 * @brief OSM taxiway/taxilane data types and fetch/load functions for LOWW.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

/// @brief A WGS-84 coordinate pair.
struct GeoPoint
{
    double lat; ///< Latitude in decimal degrees.
    double lon; ///< Longitude in decimal degrees.
};

/// @brief OSM aeroway tag classification.
enum class AerowayType { Taxiway, Taxilane, Taxiway_HoldingPoint, Unknown };

/// @brief A single OSM way element representing a taxiway or taxilane segment.
struct OsmWay
{
    int64_t               id;       ///< OSM way ID.
    AerowayType           type;     ///< Aeroway classification.
    std::string           ref;      ///< Taxiway letter/name from tags["ref"]; empty if absent.
    std::string           name;     ///< Human-readable name from tags["name"]; empty if absent.
    std::vector<GeoPoint> geometry; ///< Ordered coordinate nodes forming the way.
};

/// @brief All taxiway/taxilane ways fetched for the configured airport.
struct OsmAirportData
{
    std::vector<OsmWay> ways; ///< Taxiway and taxilane way segments.
};

/// @brief Convenience alias used for async futures and return values.
using OsmResult = std::expected<OsmAirportData, std::string>;

/// @brief Fetches LOWW taxiway/taxilane geometry from the Overpass API and saves it to the local cache.
/// @return Populated OsmAirportData on success, or an error string on any network or parse failure.
/// @note Performs a synchronous HTTPS POST; always call from a background thread.
OsmResult fetchLOWWTaxiways();

/// @brief Loads previously cached taxiway data from osm_taxiways_LOWW.json in the plugin directory.
/// @return Populated OsmAirportData on success, or an error string if the file is absent or malformed.
/// @note Reads from disk synchronously; suitable for background-thread use at startup.
OsmResult loadCachedTaxiways();
