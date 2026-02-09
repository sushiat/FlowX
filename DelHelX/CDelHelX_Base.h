#pragma once
#include <EuroScope/EuroScopePlugIn.h>

#include "RadarScreen.h"

class CDelHelX_Base : public EuroScopePlugIn::CPlugIn
{
public:
	CDelHelX_Base();
	EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

protected:
	bool debug;
	RadarScreen* radarScreen;
	EuroScopePlugIn::CFlightPlanList twrSameSID;
	void PushToOtherControllers(EuroScopePlugIn::CFlightPlan& fp) const;
};

