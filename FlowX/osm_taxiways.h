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
enum class AerowayType { Taxiway, Taxilane, Taxiway_HoldingPoint, Taxiway_Intersection, Unknown };

/// @brief A single OSM way element representing a taxiway or taxilane segment.
struct OsmWay
{
    int64_t               id;       ///< OSM way ID.
    AerowayType           type;     ///< Aeroway classification.
    std::string           ref;      ///< Taxiway letter/name from tags["ref"]; empty if absent.
    std::string           name;     ///< Human-readable name from tags["name"]; empty if absent.
    std::vector<GeoPoint> geometry; ///< Ordered coordinate nodes forming the way.
};

/// @brief A single OSM node element representing a runway holding position (aeroway=holding_position).
struct OsmHoldingPosition
{
    int64_t     id;   ///< OSM node ID.
    std::string ref;  ///< Designator from tags["ref"]; empty if absent.
    std::string name; ///< Human-readable name from tags["name"]; empty if absent.
    GeoPoint    pos;  ///< WGS-84 coordinate of the node.
};

/// @brief All taxiway/taxilane ways and holding position nodes fetched for the configured airport.
struct OsmAirportData
{
    std::vector<OsmWay>             ways;             ///< Taxiway and taxilane way segments.
    std::vector<OsmHoldingPosition> holdingPositions; ///< Runway holding position nodes (aeroway=holding_position).
    bool                            preAnnotated = false; ///< True when loaded from a cache file that already contains annotated types; skips re-annotation in PollOsmFuture.
};

/// @brief Convenience alias used for async futures and return values.
using OsmResult = std::expected<OsmAirportData, std::string>;

/// @brief Returns the total length of a way's geometry in metres using the haversine formula.
/// @return 0.0 for ways with fewer than two nodes.
double WayLengthM(const OsmWay& way);

/// @brief Serialises annotated OsmAirportData to the cache JSON file in the plugin directory.
/// @note Sets "annotated": true so the cache is loaded without re-running type annotation.
/// @note Best-effort: silently ignores file I/O failures.
void SaveOsmCache(const OsmAirportData& data);

/// @brief Fetches LOWW taxiway/taxilane geometry from the Overpass API and saves it to the local cache.
/// @return Populated OsmAirportData on success, or an error string on any network or parse failure.
/// @note Performs a synchronous HTTPS POST; always call from a background thread.
OsmResult fetchLOWWTaxiways();

/// @brief Loads previously cached taxiway data from osm_taxiways_LOWW.json in the plugin directory.
/// @return Populated OsmAirportData on success, or an error string if the file is absent or malformed.
/// @note Reads from disk synchronously; suitable for background-thread use at startup.
OsmResult loadCachedTaxiways();
