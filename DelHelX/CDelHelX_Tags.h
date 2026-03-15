#pragma once
#include "CDelHelX_Timers.h"
#include "tagInfo.h"

class CDelHelX_Tags : public CDelHelX_Timers
{
protected:
	bool groundOverride = false;
	bool towerOverride = false;
	bool noChecks = false;

	tagInfo GetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	tagInfo GetTaxiOutTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	tagInfo GetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetSameSidTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetTakeoffTimerTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetTakeoffDistanceTag(EuroScopePlugIn::CFlightPlan& fp);
	static tagInfo GetAssignedRunwayTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetTttTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	tagInfo GetInboundNmTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetSuggestedVacateTag(EuroScopePlugIn::CFlightPlan& fp);
	tagInfo GetHoldingPointTag(EuroScopePlugIn::CFlightPlan& fp, int index);
	tagInfo GetDepartureInfoTag(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
};
