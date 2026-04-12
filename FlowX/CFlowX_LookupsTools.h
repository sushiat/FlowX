/**
 * @file CFlowX_LookupsTools.h
 * @brief Declaration of CFlowX_LookupsTools, the geometry and lookup utility layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_Settings.h"

/// @brief Plugin layer providing geometric helper functions and holding-point utilities.
class CFlowX_LookupsTools : public CFlowX_Settings
{
  protected:
    /// @brief Encodes a holding-point name into the flight-strip annotation string in slot 8.
    /// @param annotation Current annotation string from slot 8.
    /// @param hp Holding-point name to append.
    /// @return Updated annotation string with the HP placed from position 7 onward.
    /// @note Slot 8 layout: [0] QNH flag ('Q' = new QNH), [1..6] transfer frequency with dot removed (6 chars, spaces = not transferred), [7..] holding point name.
    static std::string AppendHoldingPointToFlightStripAnnotation(const std::string& annotation, const std::string& hp);

    /// @brief Converts a colour name string to the corresponding COLORREF constant.
    /// @param colorName Colour name (e.g. "green", "orange", "turq", "purple", "red", "white", "yellow").
    /// @return Matching COLORREF, or TAG_COLOR_DEFAULT_GRAY if the name is not recognised.
    static COLORREF ColorFromString(const std::string& colorName);

    /// @brief Returns the bearing in degrees from one geographic position to another.
    /// @param fromLat Latitude of the origin (decimal degrees).
    /// @param fromLon Longitude of the origin (decimal degrees).
    /// @param toLat Latitude of the destination (decimal degrees).
    /// @param toLon Longitude of the destination (decimal degrees).
    /// @return Bearing in degrees (0–360).
    [[nodiscard]] static double BearingBetween(double fromLat, double fromLon, double toLat, double toLon);

    /// @brief Returns the bearing in degrees from a runway threshold to the given position.
    /// @param rwy Runway designator string.
    /// @param currentPosition Position to measure to.
    /// @param runways Map of runways for the airport.
    /// @return Bearing in degrees (0–360), or -1 if the runway is not found.
    static double DirectionFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways);

    /// @brief Returns the great-circle distance in NM from the given runway's threshold to a position.
    /// @param rwy Runway designator string.
    /// @param currentPosition Position to measure from.
    /// @param runways Map of runways for the airport.
    /// @return Distance in nautical miles, or 0.0 if the runway is not found.
    static double DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition, const std::map<std::string, runway>& runways);

    /// @brief Returns a numeric ranking for an aircraft wake-turbulence category character.
    /// @param wtc Wake-turbulence category character (J, H, M, L; case-insensitive).
    /// @return Ranking value: J=4, H=3, M=2, L=1, unknown=0.
    [[nodiscard]] static int GetAircraftWeightCategoryRanking(char wtc);

    /// @brief Tests whether a position lies within the physical bounds of a runway.
    /// @param rwy The runway to test against (provides near threshold, width, and opposite designator).
    /// @param runways Full runway map for the airport (used to look up the opposite threshold).
    /// @param pos Position to test.
    /// @return True if the position is within the runway rectangle (centerline ± half-width, between the two thresholds).
    /// @note Returns false if @p rwy has no width configured or the opposite runway is not in the map.
    [[nodiscard]] static bool IsPositionOnRunway(const runway& rwy, const std::map<std::string, runway>& runways, const EuroScopePlugIn::CPosition& pos);

    /// @brief Checks whether two holding-point names refer to the same physical point.
    /// @param hp1 First holding-point name.
    /// @param hp2 Second holding-point name.
    /// @param runways Map of runways for the airport.
    /// @return Non-zero if the points are considered the same (identical names or linked via sameAs).
    [[nodiscard]] static int IsSameHoldingPoint(std::string hp1, std::string hp2, const std::map<std::string, runway>& runways);

  public:
    /// @brief Tests whether the point (x, y) lies inside a polygon defined by parallel coordinate arrays.
    /// @note Uses the winding-number algorithm; vertices must be in sequential traversal order (CW or CCW).
    [[nodiscard]] static bool PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y);
};
