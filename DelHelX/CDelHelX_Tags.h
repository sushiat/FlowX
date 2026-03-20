#pragma once
#include "CDelHelX_Timers.h"
#include "tagInfo.h"

/// @brief Plugin layer that computes the text and colour for every tag item column.
///
/// Each Get*Tag method produces a tagInfo result that is returned to EuroScope via OnGetTagItem().
class CDelHelX_Tags : public CDelHelX_Timers
{
protected:
	bool groundOverride = false; ///< When true, behaves as if a ground station is online (for testing)
	bool towerOverride  = false; ///< When true, behaves as if a tower station is online (for testing)
	bool noChecks       = false; ///< When true, skips flight-plan validation checks (offline testing only)

	/// @brief Builds the Push+Start helper tag showing the next applicable frequency or a validation error.
	/// @param fp Flight plan being evaluated.
	/// @param rt Correlated radar target.
	/// @return tagInfo with frequency string and colour, or an error code in red.
	tagInfo GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

	/// @brief Builds the taxi-out indicator tag ("T" for taxi-out stand, "P" for push stand).
	/// @param fp Flight plan being evaluated.
	/// @param rt Correlated radar target.
	/// @return tagInfo with "T", "P", or empty text.
	tagInfo GetTaxiOutTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

	/// @brief Builds the new-QNH tag showing an orange "X" when a QNH change was received.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with "X" in orange, or empty.
	tagInfo GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the same-SID tag showing the SID name colour-coded by group.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with the SID name and its configured colour.
	tagInfo GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the takeoff spacing tag showing time or distance separation from the previous departure.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with spacing text and a colour indicating compliance.
	tagInfo GetTakeoffSpacingTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the assigned runway tag showing the flight-plan departure runway.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with the runway designator string.
	static tagInfo GetAssignedRunwayTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the TTT (time-to-touchdown) tag for an inbound aircraft.
	/// @param fp Flight plan being evaluated.
	/// @param rt Correlated radar target.
	/// @return tagInfo with time string and colour indicating urgency.
	tagInfo GetTttTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

	/// @brief Builds the inbound NM tag showing distance to the runway threshold.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with distance string and distance-based colour coding.
	tagInfo GetInboundNmTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the suggested vacate point tag based on assigned stand and runway configuration.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with the suggested vacate point name, or empty if none applies.
	tagInfo GetSuggestedVacateTag(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds a holding-point tag for the given slot index.
	/// @param fp Flight plan being evaluated.
	/// @param index Slot index (1 = HP1, 2 = HP2, 3 = HP3, 4 = HPO).
	/// @return tagInfo with the holding-point name and colour (orange if starred/requested).
	tagInfo GetHoldingPointTag(EuroScopePlugIn::CFlightPlan& fp, int index);

	/// @brief Builds the departure-info tag summarising SID, transfer status, and clearance state.
	/// @param fp Flight plan being evaluated.
	/// @param rt Correlated radar target.
	/// @return tagInfo with departure information string and colour.
	tagInfo GetDepartureInfoTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

	/// @brief Builds the TWR next-frequency tag showing the handoff frequency for a departing aircraft.
	/// @param fp Flight plan being evaluated.
	/// @param rt Correlated radar target.
	/// @return tagInfo with "->frequency" string and urgency-coded colour.
	tagInfo GetTwrNextFreqTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);

	/// @brief Builds the tower sort key tag used to order the TWR departure list.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with the sort key string.
	tagInfo GetTwrSortKey(EuroScopePlugIn::CFlightPlan& fp);

	/// @brief Builds the expanded ground-state tag with a human-readable label.
	/// @param fp Flight plan being evaluated.
	/// @return tagInfo with the expanded state label and colour.
	tagInfo GetGndStateExpandedTag(EuroScopePlugIn::CFlightPlan& fp);
};
