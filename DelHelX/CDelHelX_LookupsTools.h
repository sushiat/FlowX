#pragma once
#include "CDelHelX_Settings.h"

/// @brief Plugin layer providing geometric helper functions and holding-point utilities.
///
/// All methods are static so they can be called without an object instance where needed.
class CDelHelX_LookupsTools : public CDelHelX_Settings
{
protected:
    /// @brief Tests whether the point (x, y) lies inside a polygon defined by parallel coordinate arrays.
    /// @param polyCorners Number of polygon vertices.
    /// @param polyX Array of polygon X (longitude) coordinates, length @p polyCorners.
    /// @param polyY Array of polygon Y (latitude) coordinates, length @p polyCorners.
    /// @param x X (longitude) of the point to test.
    /// @param y Y (latitude) of the point to test.
    /// @return True if the point is inside the polygon.
    /// @note Uses the ray-casting algorithm; behaviour on edge points is unspecified.
    static bool PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y);

    /// @brief Returns the great-circle distance in NM from the given runway's threshold to a position.
    /// @param rwy Runway designator string.
    /// @param currentPosition Position to measure from.
    /// @param runways Map of runways for the airport.
    /// @return Distance in nautical miles, or 0.0 if the runway is not found.
    static double DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways);

    /// @brief Returns the bearing in degrees from a runway threshold to the given position.
    /// @param rwy Runway designator string.
    /// @param currentPosition Position to measure to.
    /// @param runways Map of runways for the airport.
    /// @return Bearing in degrees (0–360), or -1 if the runway is not found.
    static double DirectionFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways);

    /// @brief Checks whether a holding-point name begins with the given HP name and matches the given index.
    /// @param rwy Runway designator string.
    /// @param hp Holding-point name to check.
    /// @param index Expected index value.
    /// @param runways Map of runways for the airport.
    /// @return True if a matching holding point exists on the runway.
    static bool MatchesRunwayHoldingPoint(const std::string& rwy, const std::string& hp, int index, const std::map<std::string, runway>& runways);

    /// @brief Returns the name of the first non-assignable holding point on a runway for a given index.
    /// @param rwy Runway designator string.
    /// @param index Slot index to look up.
    /// @param runways Map of runways for the airport.
    /// @return Holding-point name, or an empty string if none is found.
    static std::string GetRunwayHoldingPoint(const std::string& rwy, int index, const std::map<std::string, runway>& runways);

    /// @brief Returns a numeric ranking for an aircraft wake-turbulence category character.
    /// @param wtc Wake-turbulence category character (J, H, M, L; case-insensitive).
    /// @return Ranking value: J=4, H=3, M=2, L=1, unknown=0.
    static int GetAircraftWeightCategoryRanking(char wtc);

    /// @brief Checks whether two holding-point names refer to the same physical point.
    /// @param hp1 First holding-point name.
    /// @param hp2 Second holding-point name.
    /// @param runways Map of runways for the airport.
    /// @return Non-zero if the points are considered the same (identical names or linked via sameAs).
    static int IsSameHoldingPoint(std::string hp1, std::string hp2, const std::map<std::string, runway>& runways);

    /// @brief Encodes a holding-point name into the flight-strip annotation string in slot 8.
    /// @param annotation Current annotation string from slot 8.
    /// @param hp Holding-point name to append.
    /// @return Updated annotation string with the HP placed after the first two flag characters.
    /// @note The first two characters of annotation slot 8 are reserved for flags (Q = new QNH, T = transferred).
    static std::string AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp);

    /// @brief Converts a colour name string to the corresponding COLORREF constant.
    /// @param colorName Colour name (e.g. "green", "orange", "turq", "purple", "red", "white", "yellow").
    /// @return Matching COLORREF, or TAG_COLOR_DEFAULT_GRAY if the name is not recognised.
    static COLORREF ColorFromString(const std::string& colorName);
};
