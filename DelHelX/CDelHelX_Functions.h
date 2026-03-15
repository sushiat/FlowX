#pragma once
#include "CDelHelX_Tags.h"

class CDelHelX_Functions : public CDelHelX_Tags
{
protected:
	void Func_OnFreq(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt);
	void Func_ClearNewQnh(EuroScopePlugIn::CFlightPlan& fp);
	void Func_AssignHp(EuroScopePlugIn::CFlightPlan& fp, int index);
	void Func_RequestHp(EuroScopePlugIn::CFlightPlan& fp, int index);
	void Func_AssignHpo(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);
	void Func_RequestHpo(EuroScopePlugIn::CFlightPlan& fp, POINT Pt);
	void Func_HpoListselect(EuroScopePlugIn::CFlightPlan& fp, const char* sItemString);
	void Func_LineUp(EuroScopePlugIn::CFlightPlan& fp);
	void Func_TakeOff(EuroScopePlugIn::CFlightPlan& fp);
	void Func_TransferNext(EuroScopePlugIn::CFlightPlan& fp);
	void Func_ClrdToLand(EuroScopePlugIn::CFlightPlan& fp);
	void Func_MissedApp(EuroScopePlugIn::CFlightPlan& fp);
};
