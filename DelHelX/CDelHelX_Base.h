#pragma once
#include <EuroScope/EuroScopePlugIn.h>

#include "RadarScreen.h"

class CDelHelX_Base : public EuroScopePlugIn::CPlugIn
{
public:
	CDelHelX_Base();
	EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;
	void ClearRadarScreen() { radarScreen = nullptr; }

protected:
	bool debug;
	RadarScreen* radarScreen;
	EuroScopePlugIn::CFlightPlanList twrSameSID;
	EuroScopePlugIn::CFlightPlanList tttInbound;
	void PushToOtherControllers(EuroScopePlugIn::CFlightPlan& fp) const;
};

