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
};
