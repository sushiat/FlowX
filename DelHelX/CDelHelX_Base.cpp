#include "pch.h"
#include "CDelHelX_Base.h"

#include <filesystem>

#include "constants.h"
#include "helpers.h"
#include "date/tz.h"

CDelHelX_Base::CDelHelX_Base() : EuroScopePlugIn::CPlugIn(
	EuroScopePlugIn::COMPATIBILITY_CODE,
	PLUGIN_NAME,
	PLUGIN_VERSION,
	PLUGIN_AUTHOR,
	PLUGIN_LICENSE)
{
	this->debug = false;
	this->radarScreen = nullptr;

	std::filesystem::path base(GetPluginDirectory());
	base.append("tzdata");
	date::set_install(base.string());

	this->RegisterTagItemType("Push+Start Helper", TAG_ITEM_PS_HELPER);
	this->RegisterTagItemType("Taxi out?", TAG_ITEM_TAXIOUT);
	this->RegisterTagItemFunction("Set ONFREQ/STUP/PUSH", TAG_FUNC_ON_FREQ);
	this->RegisterTagItemType("New QNH", TAG_ITEM_NEWQNH);
	this->RegisterTagItemFunction("Clear new QNH", TAG_FUNC_CLEAR_NEWQNH);
	this->RegisterTagItemType("Same SID", TAG_ITEM_SAMESID);
	this->RegisterTagItemType("Assigned RWY", TAG_ITEM_ASSIGNED_RUNWAY);
	this->RegisterTagItemType("HP1", TAG_ITEM_HP1);
	this->RegisterTagItemFunction("Assign HP1", TAG_FUNC_ASSIGN_HP1);
	this->RegisterTagItemFunction("Request HP1", TAG_FUNC_REQUEST_HP1);
	this->RegisterTagItemType("HP2", TAG_ITEM_HP2);
	this->RegisterTagItemFunction("Assign HP2", TAG_FUNC_ASSIGN_HP2);
	this->RegisterTagItemFunction("Request HP2", TAG_FUNC_REQUEST_HP2);
	this->RegisterTagItemType("HP3", TAG_ITEM_HP3);
	this->RegisterTagItemFunction("Assign HP3", TAG_FUNC_ASSIGN_HP3);
	this->RegisterTagItemFunction("Request HP3", TAG_FUNC_REQUEST_HP3);
	this->RegisterTagItemType("HP other", TAG_ITEM_HPO);
	this->RegisterTagItemFunction("Assign other HP", TAG_FUNC_ASSIGN_HPO);
	this->RegisterTagItemFunction("Request other HP", TAG_FUNC_REQUEST_HPO);
	this->RegisterTagItemType("TIMER", TAG_ITEM_TAKEOFF_TIMER);
	this->RegisterTagItemType("NM", TAG_ITEM_TAKEOFF_DISTANCE);
	this->RegisterTagItemFunction("Line up", TAG_FUNC_LINE_UP);
	this->RegisterTagItemFunction("Take off", TAG_FUNC_TAKE_OFF);
	this->RegisterTagItemFunction("Transfer next", TAG_FUNC_TRANSFER_NEXT);
	this->RegisterTagItemType("Departure Info", TAG_ITEM_DEPARTURE_INFO);
	this->RegisterTagItemType("TTT", TAG_ITEM_TTT);
	this->RegisterTagItemType("Inbound NM", TAG_ITEM_INBOUND_NM);
	this->RegisterTagItemType("Suggested vacate", TAG_ITEM_SUGGESTED_VACATE);
	this->RegisterTagItemFunction("Cleared to Land", TAG_FUNC_CLRD_TO_LAND);
	this->RegisterTagItemFunction("Missed App", TAG_FUNC_MISSED_APP);
	this->RegisterTagItemFunction("Auto Stand Assignment", TAG_FUNC_STAND_AUTO);
	this->RegisterTagItemType("TWR Next Freq", TAG_ITEM_TWR_NEXT_FREQ);
	this->RegisterTagItemType("TWR Sort Key", TAG_ITEM_TWR_SORT);
	this->RegisterTagItemType("GND state expanded", TAG_ITEM_GND_STATE_EXPANDED);

	this->RegisterDisplayType(PLUGIN_NAME, true, false, false, false);

	this->twrSameSID = this->RegisterFpList("TWR Outbound");
	if (this->twrSameSID.GetColumnNumber() == 0)
	{
		this->twrSameSID.AddColumnDefinition("C/S", 12, false, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_CALLSIGN, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("STS", 12, false, PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, PLUGIN_NAME, TAG_FUNC_LINE_UP, PLUGIN_NAME, TAG_FUNC_TAKE_OFF);
		this->twrSameSID.AddColumnDefinition("DEP?", 10, false, PLUGIN_NAME, TAG_ITEM_DEPARTURE_INFO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("RWY", 4, false, PLUGIN_NAME, TAG_ITEM_ASSIGNED_RUNWAY, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("SID", 11, false, PLUGIN_NAME, TAG_ITEM_SAMESID, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("WTC", 4, true, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_AIRCRAFT_CATEGORY, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("ATYP", 8, false, TOPSKY_PLUGIN_NAME, TOPSKY_TAG_TYPE_ATYP, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("Freq", 15, false, PLUGIN_NAME, TAG_ITEM_PS_HELPER, PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("HP1", 4, false, PLUGIN_NAME, TAG_ITEM_HP1, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP1, PLUGIN_NAME, TAG_FUNC_REQUEST_HP1);
		this->twrSameSID.AddColumnDefinition("HP2", 4, false, PLUGIN_NAME, TAG_ITEM_HP2, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP2, PLUGIN_NAME, TAG_FUNC_REQUEST_HP2);
		this->twrSameSID.AddColumnDefinition("HP3", 4, false, PLUGIN_NAME, TAG_ITEM_HP3, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP3, PLUGIN_NAME, TAG_FUNC_REQUEST_HP3);
		this->twrSameSID.AddColumnDefinition("HPO", 4, false, PLUGIN_NAME, TAG_ITEM_HPO, PLUGIN_NAME, TAG_FUNC_ASSIGN_HPO, PLUGIN_NAME, TAG_FUNC_REQUEST_HPO);
		this->twrSameSID.AddColumnDefinition("TIMER", 17, false, PLUGIN_NAME, TAG_ITEM_TAKEOFF_TIMER, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("NM", 15, false, PLUGIN_NAME, TAG_ITEM_TAKEOFF_DISTANCE, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("S", 12, false, PLUGIN_NAME, TAG_ITEM_TWR_SORT, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
	}
	this->twrSameSID.ShowFpList(true);

	this->tttInbound = this->RegisterFpList("TWR Inbound");
	if (this->tttInbound.GetColumnNumber() == 0)
	{
		this->tttInbound.AddColumnDefinition("TTT", 12, false, PLUGIN_NAME, TAG_ITEM_TTT, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("C/S", 12, false, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_CALLSIGN, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("NM", 8, false, PLUGIN_NAME, TAG_ITEM_INBOUND_NM, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("SPD", 5, true, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_GROUND_SPEED_WOUT_N, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("WTC", 4, true, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_AIRCRAFT_CATEGORY, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("ATYP", 8, false, TOPSKY_PLUGIN_NAME, TOPSKY_TAG_TYPE_ATYP, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("Gate", 5, true, GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_ASSIGNED_STAND, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->tttInbound.AddColumnDefinition("Vacate", 7, true, PLUGIN_NAME, TAG_ITEM_SUGGESTED_VACATE, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
	}
	this->tttInbound.ShowFpList(true);
}

EuroScopePlugIn::CRadarScreen* CDelHelX_Base::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
	this->radarScreen = new RadarScreen();
	this->radarScreen->debug = this->debug;
	return this->radarScreen;
}

void CDelHelX_Base::PushToOtherControllers(EuroScopePlugIn::CFlightPlan& fp) const
{
	for (EuroScopePlugIn::CController c = this->ControllerSelectFirst(); c.IsValid(); c = this->ControllerSelectNext(c)) {
		if (c.IsController())
		{
			std::string callsign = c.GetCallsign();
			if (callsign.size() >= 3) {
				if (callsign.find("DEL") != std::string::npos || callsign.find("GND") != std::string::npos || callsign.find("TWR") != std::string::npos) {
					fp.PushFlightStrip(c.GetCallsign());
				}
			}
		}
	}
}

