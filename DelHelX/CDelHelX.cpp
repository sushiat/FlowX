#include "pch.h"

#include "CDelHelX.h"

#include <iostream>

#include "date/tz.h"

static CDelHelX* pPlugin;

CDelHelX::CDelHelX() : EuroScopePlugIn::CPlugIn(
	EuroScopePlugIn::COMPATIBILITY_CODE,
	PLUGIN_NAME,
	PLUGIN_VERSION,
	PLUGIN_AUTHOR,
	PLUGIN_LICENSE
)
{
	std::ostringstream msg;
	msg << "Version " << PLUGIN_VERSION << " loaded.";

	this->LogMessage(msg.str(), "Init");

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

	this->RegisterDisplayType(PLUGIN_NAME, true, false, false, false);

	this->twrSameSID = this->RegisterFpList("TowerHelper");
	if (this->twrSameSID.GetColumnNumber() == 0)
	{
		this->twrSameSID.AddColumnDefinition("C/S", 12, false, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_CALLSIGN, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("STS", 12, false, "Ground Radar Plugin", 3, PLUGIN_NAME, TAG_FUNC_LINE_UP, PLUGIN_NAME, TAG_FUNC_TAKE_OFF);
		this->twrSameSID.AddColumnDefinition("RWY", 4, false, PLUGIN_NAME, TAG_ITEM_ASSIGNED_RUNWAY, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("SID", 11, false, PLUGIN_NAME, TAG_ITEM_SAMESID, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("WTC", 4, true, NULL, EuroScopePlugIn::TAG_ITEM_TYPE_AIRCRAFT_CATEGORY, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("ATYP", 5, false, TOPSKY_PLUGIN_NAME, TOPSKY_TAG_TYPE_ATYP, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("HP1", 4, false, PLUGIN_NAME, TAG_ITEM_HP1, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP1, PLUGIN_NAME, TAG_FUNC_REQUEST_HP1);
		this->twrSameSID.AddColumnDefinition("HP2", 4, false, PLUGIN_NAME, TAG_ITEM_HP2, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP2, PLUGIN_NAME, TAG_FUNC_REQUEST_HP2);
		this->twrSameSID.AddColumnDefinition("HP3", 4, false, PLUGIN_NAME, TAG_ITEM_HP3, PLUGIN_NAME, TAG_FUNC_ASSIGN_HP3, PLUGIN_NAME, TAG_FUNC_REQUEST_HP3);
		this->twrSameSID.AddColumnDefinition("HPO", 4, false, PLUGIN_NAME, TAG_ITEM_HPO, PLUGIN_NAME, TAG_FUNC_ASSIGN_HPO, PLUGIN_NAME, TAG_FUNC_REQUEST_HPO);
		this->twrSameSID.AddColumnDefinition("TIMER", 8, false, PLUGIN_NAME, TAG_ITEM_TAKEOFF_TIMER, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("NM", 8, false, PLUGIN_NAME, TAG_ITEM_TAKEOFF_DISTANCE, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
		this->twrSameSID.AddColumnDefinition("Freq", 15, false, PLUGIN_NAME, TAG_ITEM_PS_HELPER, PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, NULL, EuroScopePlugIn::TAG_ITEM_FUNCTION_NO);
	}
	this->twrSameSID.ShowFpList(true);

	this->updateCheck = false;
	this->flashOnMessage = false;
	this->groundOverride = false;
	this->towerOverride = false;
	this->noChecks = false;

	this->LoadSettings();
	this->LoadConfig();

	std::filesystem::path base(GetPluginDirectory());
	base.append("tzdata");
	date::set_install(base.string());

	if (this->updateCheck) {
		this->latestVersion = std::async(FetchLatestVersion);
	}
}

CDelHelX::~CDelHelX() = default;

EuroScopePlugIn::CRadarScreen* CDelHelX::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
	this->radarScreen = new RadarScreen();
	this->radarScreen->debug = this->debug;
	return this->radarScreen;
}

bool CDelHelX::OnCompileCommand(const char* sCommandLine)
{
	std::vector<std::string> args = split(sCommandLine);

	if (starts_with(args[0], ".delhelx"))
	{
		if (args.size() == 1)
		{
			std::ostringstream msg;
			msg << "Version " << PLUGIN_VERSION << " loaded. Available commands: gnd, twr, nocheck, reset, update, flash, redoflags, testqnh";

			this->LogMessage(msg.str(), "Init");

			return true;
		}

		if (args[1] == "debug") {
			if (this->debug) {
				this->LogMessage("Disabling debug mode", "Debug");
			}
			else {
				this->LogMessage("Enabling debug mode", "Debug");
			}

			this->debug = !this->debug;
			this->radarScreen->debug = this->debug;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "update")
		{
			if (this->updateCheck)
			{
				this->LogMessage("Disabling update check", "Update");
			}
			else {
				this->LogMessage("Enabling update check", "Update");
			}

			this->updateCheck = !this->updateCheck;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "flash")
		{
			if (this->flashOnMessage)
			{
				this->LogMessage("No longer flashing on DelHelX message", "Config");
			}
			else {
				this->LogMessage("Flashing on DelHelX message", "Config");
			}

			this->flashOnMessage = !this->flashOnMessage;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "gnd")
		{
			if (this->groundOverride)
			{
				this->LogMessage("GND freq override OFF", "GND");
			}
			else {
				this->LogMessage("GND freq override ON", "GND");
			}

			this->groundOverride = !this->groundOverride;

			return true;
		}
		else if (args[1] == "twr")
		{
			if (this->towerOverride)
			{
				this->LogMessage("TWR freq override OFF", "TWR");
			}
			else {
				this->LogMessage("TWR freq override ON", "TWR");
			}

			this->towerOverride = !this->towerOverride;

			return true;
		}
		else if (args[1] == "nocheck")
		{
			if (this->noChecks)
			{
				this->LogMessage("Flight plan checks turned ON", "Checks");
			}
			else {
				this->LogMessage("Flight plan checks turned OFF, use only for testing!!!", "Checks");
			}

			this->noChecks = !this->noChecks;

			return true;
		}
		else if (args[1] == "reset")
		{
			this->LogMessage("Resetting DelHelX plugin to defaults", "Defaults");
			this->updateCheck = false;
			this->flashOnMessage = false;
			this->groundOverride = false;
			this->towerOverride = false;
			this->noChecks = false;

			this->SaveSettings();

			return true;
		}
		else if (args[1] == "redoflags")
		{
			this->LogMessage("Redoing clearance flags...", "Flags");
			this->RedoFlags();

			return true;
		}
		else if (args[1] == "testqnh")
		{
			this->OnNewMetarReceived("LOWW", "LOWW 231805Z 26011KT CAVOK 15/07 Q2000 TEMPO 32015KT");

			return true;
		}
	}

	return false;
}

void CDelHelX::RedoFlags()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		// Skip if aircraft is not on the ground (currently using ground speed threshold)
		// TODO better option for finding aircraft on ground??? maybe airport elevation via config???
		if (!pos.IsValid() || pos.GetReportedGS() > 40) {
			continue;
		}

		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		// Skip if aircraft is tracked (with exception of aircraft tracked by current controller)
		if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		std::string cs = fp.GetCallsign();

		// Skip aircraft without a valid flightplan (no departure/destination airport)
		if (dep.empty() || arr.empty()) {
			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			continue;
		}

		if (fp.GetClearenceFlag())
		{
			// Toggle off and back on
			this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
			this->radarScreen->StartTagFunction(cs.c_str(), nullptr, 0, cs.c_str(), nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, POINT(), RECT());
		}
	}
}

void CDelHelX::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
	if (!FlightPlan.IsValid())
	{
		return;
	}

	if (ItemCode == TAG_ITEM_PS_HELPER)
	{
		validation res = this->CheckPushStartStatus(FlightPlan, RadarTarget);

		if (res.valid)
		{
			strcpy_s(sItemString, 16, res.tag.c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;

			if (res.color == TAG_COLOR_NONE)
			{
				*pRGB = TAG_COLOR_GREEN;
			}
			else {
				*pRGB = res.color;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, res.tag.c_str());

			if (res.color != TAG_COLOR_NONE)
			{
				*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
				*pRGB = res.color;
			}
		}
	}
	else if (ItemCode == TAG_ITEM_TAXIOUT)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string dep = fpd.GetOrigin();
		to_upper(dep);

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			return;
		}

		EuroScopePlugIn::CPosition position = RadarTarget.GetPosition().GetPosition();

		std::string groundState = FlightPlan.GetGroundState();
		if (groundState.empty() || groundState == "STUP")
		{
			bool isTaxiOut = false;
			for (auto& taxiOut : airport->second.taxiOutStands)
			{
				u_int corners = taxiOut.second.lat.size();
				double lat[10], lon[10];
				std::copy(taxiOut.second.lat.begin(), taxiOut.second.lat.end(), lat);
				std::copy(taxiOut.second.lon.begin(), taxiOut.second.lon.end(), lon);

				if (CDelHelX::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
				{
					isTaxiOut = true;
					continue;
				}
			}

			if (isTaxiOut)
			{
				strcpy_s(sItemString, 16, "T");
				*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
				*pRGB = TAG_COLOR_GREEN;
			}
			else
			{
				if (groundState.empty())
				{
					strcpy_s(sItemString, 16, "P");
				}
				else
				{
					strcpy_s(sItemString, 16, "");
				}
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_NEWQNH)
	{
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		std::string annotation = fpcad.GetFlightStripAnnotation(2);
		if (annotation == "NQNH")
		{
			strcpy_s(sItemString, 16, "X");
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_ORANGE;
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_SAMESID)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();

		std::string rwy = fpd.GetDepartureRwy();
		std::string sid = fpd.GetSidName();
		auto sidKey = sid.substr(0, sid.length() - 2);

		strcpy_s(sItemString, 16, sid.c_str());
		*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
		*pRGB = TAG_COLOR_WHITE;
		*pFontSize += 5.0;

		if (rwy == "11")
		{
			std::map<std::string, COLORREF> rwy11SameSidColors{
				{"LANUX", TAG_COLOR_GREEN}, {"BUWUT", TAG_COLOR_GREEN}, {"LEDVA", TAG_COLOR_GREEN},
				{"OSPEN", TAG_COLOR_ORANGE}, {"RUPET", TAG_COLOR_ORANGE},
				{"STEIN", TAG_COLOR_TURQ}, {"ARSIN", TAG_COLOR_TURQ},
				{"KOXER", TAG_COLOR_PURPLE}, {"ADAMA", TAG_COLOR_PURPLE},
				{"MEDIX", TAG_COLOR_RED}, {"LUGEM", TAG_COLOR_RED},

				{"IRGO", TAG_COLOR_ORANGE}, {"IMVO", TAG_COLOR_ORANGE}, {"ODSU", TAG_COLOR_ORANGE}, {"OSMO", TAG_COLOR_ORANGE}, {"MEDIX", TAG_COLOR_ORANGE}
			};

			if (rwy11SameSidColors.find(sidKey) != rwy11SameSidColors.end())
			{
				*pRGB = rwy11SameSidColors.at(sidKey);
			}
		}

		if (rwy == "16")
		{
			std::map<std::string, COLORREF> rwy16SameSidColors{
				{"LANUX", TAG_COLOR_GREEN}, {"BUWUT", TAG_COLOR_GREEN}, {"LEDVA", TAG_COLOR_GREEN},
				{"MEDIX", TAG_COLOR_RED}, {"LUGEM", TAG_COLOR_RED},
				{"OSPEN", TAG_COLOR_ORANGE}, {"RUPET", TAG_COLOR_ORANGE},
				{"STEIN", TAG_COLOR_TURQ}, {"ARSIN", TAG_COLOR_TURQ},
				{"KOXER", TAG_COLOR_PURPLE}, {"ADAMA", TAG_COLOR_PURPLE},
			};

			if (rwy16SameSidColors.find(sidKey) != rwy16SameSidColors.end())
			{
				*pRGB = rwy16SameSidColors.at(sidKey);
			}
		}

		if (rwy == "29")
		{
			std::map<std::string, COLORREF> rwy29SameSidColors{
				{"LANUX", TAG_COLOR_GREEN}, {"BUWUT", TAG_COLOR_GREEN}, {"LEDVA", TAG_COLOR_GREEN},
				{"MEDIX", TAG_COLOR_PURPLE}, {"LUGEM", TAG_COLOR_PURPLE},
				{"OSPEN", TAG_COLOR_ORANGE}, {"RUPET", TAG_COLOR_ORANGE},
				{"STEIN", TAG_COLOR_TURQ}, {"ARSIN", TAG_COLOR_TURQ}, {"KOXER", TAG_COLOR_TURQ}, {"ADAMA", TAG_COLOR_TURQ},

				{"IRGO", TAG_COLOR_GREEN}, {"IMVO", TAG_COLOR_GREEN}, {"ODSU", TAG_COLOR_GREEN}, {"OSMO", TAG_COLOR_GREEN}, {"OTGA", TAG_COLOR_GREEN},
				{"UMSU", TAG_COLOR_PURPLE}, {"UNGU", TAG_COLOR_PURPLE}, {"VABG", TAG_COLOR_PURPLE},
				{"EMKO", TAG_COLOR_ORANGE}, {"EWUK", TAG_COLOR_ORANGE},
				{"AGMI", TAG_COLOR_TURQ}, {"ASPI", TAG_COLOR_TURQ}
			};

			if (rwy29SameSidColors.find(sidKey) != rwy29SameSidColors.end())
			{
				*pRGB = rwy29SameSidColors.at(sidKey);
			}
		}

		if (rwy == "34")
		{
			std::map<std::string, COLORREF> rw34SameSidColors{
				{"LANUX", TAG_COLOR_GREEN}, {"BUWUT", TAG_COLOR_GREEN}, {"LEDVA", TAG_COLOR_GREEN},
				{"STEIN", TAG_COLOR_TURQ}, {"ARSIN", TAG_COLOR_TURQ}, {"RUPET", TAG_COLOR_TURQ}, {"OSPEN", TAG_COLOR_TURQ},
				{"KOXER", TAG_COLOR_PURPLE}, {"ADAMA", TAG_COLOR_PURPLE},
				{"MEDIX", TAG_COLOR_RED}, {"LUGEM", TAG_COLOR_RED},

				{"IRGO", TAG_COLOR_TURQ}, {"IMVO", TAG_COLOR_TURQ}, {"ODSU", TAG_COLOR_TURQ}, {"OSMO", TAG_COLOR_TURQ}, {"OTGA", TAG_COLOR_TURQ},
				{"EMKO", TAG_COLOR_RED}, {"EWUK", TAG_COLOR_RED}
			};

			if (rw34SameSidColors.find(sidKey) != rw34SameSidColors.end())
			{
				*pRGB = rw34SameSidColors.at(sidKey);
			}
		}
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_TIMER)
	{
		std::string callSign = FlightPlan.GetCallsign();
		if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
		{
			unsigned long now = GetTickCount();
			auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;

			auto minutes = seconds / 60;
			seconds = seconds % 60;
			auto leadingSeconds = seconds <= 9 ? "0" : "";

			std::string printSeconds = std::to_string(minutes) + ":" + leadingSeconds + std::to_string(seconds);
			strcpy_s(sItemString, 16, printSeconds.c_str());
		}
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_DISTANCE)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		auto position = FlightPlan.GetCorrelatedRadarTarget().GetPosition().GetPosition();
		auto distance = DistanceFromRunwayThreshold(rwy, position);
		std::string num_text = std::to_string(distance);
		std::string rounded = num_text.substr(0, num_text.find('.') + 3);
		if (distance < 10.0)
		{
			rounded = "0" + rounded;
		}

		strcpy_s(sItemString, 16, rounded.c_str());
	}
	else if (ItemCode == TAG_ITEM_ASSIGNED_RUNWAY)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();

		strcpy_s(sItemString, 16, rwy.c_str());
	}
	else if (ItemCode == TAG_ITEM_HP1)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		std::string annotation = fpcad.GetFlightStripAnnotation(3);
		if (MatchesRunwayHoldingPoint(rwy, annotation, 1))
		{
			strcpy_s(sItemString, 16, annotation.c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (annotation.find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			std::string callSign = FlightPlan.GetCallsign();
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_NONE;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HP2)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		std::string annotation = fpcad.GetFlightStripAnnotation(3);
		if (MatchesRunwayHoldingPoint(rwy, annotation, 2))
		{
			strcpy_s(sItemString, 16, annotation.c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (annotation.find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			std::string callSign = FlightPlan.GetCallsign();
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_NONE;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HP3)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		std::string annotation = fpcad.GetFlightStripAnnotation(3);
		if (MatchesRunwayHoldingPoint(rwy, annotation, 3))
		{
			strcpy_s(sItemString, 16, annotation.c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (annotation.find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			std::string callSign = FlightPlan.GetCallsign();
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_NONE;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
	else if (ItemCode == TAG_ITEM_HPO)
	{
		EuroScopePlugIn::CFlightPlanData fpd = FlightPlan.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = FlightPlan.GetControllerAssignedData();
		std::string annotation = fpcad.GetFlightStripAnnotation(3);
		if (MatchesRunwayHoldingPoint(rwy, annotation, 4))
		{
			strcpy_s(sItemString, 16, annotation.c_str());
			*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
			*pRGB = TAG_COLOR_GREEN;

			if (annotation.find('*') != std::string::npos)
				*pRGB = TAG_COLOR_ORANGE;

			std::string callSign = FlightPlan.GetCallsign();
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end() && this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				*pRGB = TAG_COLOR_NONE;
			}
		}
		else
		{
			strcpy_s(sItemString, 16, "");
		}
	}
}

void CDelHelX::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
	EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectASEL();
	if (!fp.IsValid()) {
		return;
	}

	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
	{
		// Airport not in config
		return;
	}

	EuroScopePlugIn::CRadarTarget rt = fp.GetCorrelatedRadarTarget();

	if (FunctionId == TAG_FUNC_ON_FREQ) {
		validation res = this->CheckPushStartStatus(fp, rt);
		if (res.valid)
		{
			// Are we ground or higher?
			if (this->ControllerMyself().GetFacility() >= 3)
			{
				EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();

				bool isTaxiOut = false;
				for (auto& taxiOut : airport->second.taxiOutStands)
				{
					u_int corners = taxiOut.second.lat.size();
					double lat[10], lon[10];
					std::copy(taxiOut.second.lat.begin(), taxiOut.second.lat.end(), lat);
					std::copy(taxiOut.second.lon.begin(), taxiOut.second.lon.end(), lon);

					if (CDelHelX::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
					{
						isTaxiOut = true;
						continue;
					}
				}

				if (isTaxiOut)
				{
					std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
					fp.GetControllerAssignedData().SetScratchPadString("ST-UP");
					fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
				}
				else
				{
					// We could give PUSH here, but it's better to visually check, so let's just "pop" it up using ONFREQ
					std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
					fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
					fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
				}
			}
			else
			{
				// We are delivery set ONFREQ
				std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
				fp.GetControllerAssignedData().SetScratchPadString("ONFREQ");
				fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
			}
		}
	}
	else if (FunctionId == TAG_FUNC_CLEAR_NEWQNH)
	{
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(2, "");
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP1)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, GetRunwayHoldingPoint(rwy, 1).c_str());
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP2)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, GetRunwayHoldingPoint(rwy, 2).c_str());
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HP3)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, GetRunwayHoldingPoint(rwy, 3).c_str());
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP1)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, (GetRunwayHoldingPoint(rwy, 1) + "*").c_str());
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP2)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, (GetRunwayHoldingPoint(rwy, 2) + "*").c_str());
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HP3)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, (GetRunwayHoldingPoint(rwy, 3) + "*").c_str());
	}
	else if (FunctionId == TAG_FUNC_ASSIGN_HPO)
	{
		RECT area;
		area.left = Pt.x;
		area.right = Pt.x + 100;
		area.top = Pt.y;
		area.bottom = Pt.y + 100;
		this->OpenPopupList(area, "Assign HP", 1);

		std::string rwy = fpd.GetDepartureRwy();
		if (rwy == "29")
		{
			this->AddPopupListElement("A4", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A6", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A8", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "11")
		{
			this->AddPopupListElement("A9", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A7", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "16")
		{
			this->AddPopupListElement("B5", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("B7", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "34")
		{
			this->AddPopupListElement("B8", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("B6", "", TAG_FUNC_HPO_LISTSELECT);
		}
	}
	else if (FunctionId == TAG_FUNC_REQUEST_HPO)
	{
		RECT area;
		area.left = Pt.x;
		area.right = Pt.x + 100;
		area.top = Pt.y;
		area.bottom = Pt.y + 100;
		this->OpenPopupList(area, "Request HP", 1);

		std::string rwy = fpd.GetDepartureRwy();
		if (rwy == "29")
		{
			this->AddPopupListElement("A4*", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A6*", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A8*", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "11")
		{
			this->AddPopupListElement("A9*", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("A7*", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "16")
		{
			this->AddPopupListElement("B5*", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("B7*", "", TAG_FUNC_HPO_LISTSELECT);
		}
		if (rwy == "34")
		{
			this->AddPopupListElement("B8*", "", TAG_FUNC_HPO_LISTSELECT);
			this->AddPopupListElement("B6*", "", TAG_FUNC_HPO_LISTSELECT);
		}
	}
	else if (FunctionId == TAG_FUNC_HPO_LISTSELECT)
	{
		std::string rwy = fpd.GetDepartureRwy();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		fpcad.SetFlightStripAnnotation(3, sItemString);
	}
	else if (FunctionId == TAG_FUNC_LINE_UP)
	{
		std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
		fp.GetControllerAssignedData().SetScratchPadString("LINEUP");
		fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
	}
	else if (FunctionId == TAG_FUNC_TAKE_OFF)
	{
		std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
		fp.GetControllerAssignedData().SetScratchPadString("DEPA");
		fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());

		fp.StartTracking();
	}
	else if (FunctionId == TAG_FUNC_TRANSFER_NEXT)
	{
		std::string targetController = fp.GetCoordinatedNextController();
		if (!targetController.empty() && this->ControllerMyself().GetFacility() >= 4)
		{
			fp.InitiateHandoff(targetController.c_str());
		}
		else
		{
			fp.EndTracking();
		}
	}
}

validation CDelHelX::CheckPushStartStatus(EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget& rt)
{
	validation res{
		true, // valid
		"", // tag
		TAG_COLOR_NONE // color
	};

	std::string cs = fp.GetCallsign();
	std::string groundState = fp.GetGroundState();
	EuroScopePlugIn::CController me = this->ControllerMyself();
	EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
	std::string dep = fpd.GetOrigin();
	to_upper(dep);
	std::string rwy = fpd.GetDepartureRwy();
	std::string sid = fpd.GetSidName();

	auto airport = this->airports.find(dep);
	if (airport == this->airports.end())
	{
		// Airport not in config, so ignore it
		return res;
	}

	if (!groundState.empty())
	{
		if (groundState == "TAXI" || groundState == "DEPA") {
			// Find next frequency
			if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3 && me.GetFacility() <= 4)
			{
				// Only show tower to ground, but not to tower
				if (me.GetFacility() == 3) {
					bool towerOnline = false;
					for (auto station : this->radarScreen->towerStations)
					{
						if (station.find(dep) != std::string::npos)
						{
							towerOnline = true;
							continue;
						}
					}

					if (towerOnline || this->towerOverride)
					{
						for (auto rwyFreq : airport->second.rwyTwrFreq)
						{
							if (rwy == rwyFreq.first)
							{
								res.tag += "->" + rwyFreq.second;
								return res;
							}
						}

						// Didn't find a runway specific tower, so return default
						res.tag += "->" + airport->second.twrFreq;
						return res;
					}
				}

				for (auto station : this->radarScreen->approachStations)
				{
					if (station.find(dep) != std::string::npos)
					{
						// Search for SID-specific freq
						auto sid125 = std::set<std::string>{ "BUWUT2A", "LANUX4A", "LEDVA4A", "ADAMA2B", "BUWUT2B", "KOXER2B", "LANUX6B", "LEDVA3B", "ADAMA1A", "ADAMA1F", "ADAMA1D", "BUWUT1E", "BUWUT1F", "BUWUT1D", "KOXER1A", "KOXER1D", "KOXER1F", "LANUX1E", "LANUX1F", "LANUX6D", "LEDVA1E", "LEDVA1F", "LEDVA4D", "LOWW1A", "LOWW1B", "LOWW1D" };
						auto sid129 = std::set<std::string>{ "ARSIN2A", "LUGEM2A", "MEDIX2A", "OSPEN3A", "RUPET2A", "SOVIL2A", "STEIN3A", "ARSIN1E", "LUGEM1E", "MEDIX1E", "OSPEN1E", "RUPET1E", "SOVIL1E", "STEIN1E", "IMVO3A", "IRGO1A", "ODSU1A", "OSMO1A" };
						auto sid134 = std::set<std::string>{ "ADAMA2C", "ARSIN1B", "ARSIN1C", "ARSIN1D", "BUWUT1C", "KOXER1C", "LANUX2C", "LEDVA3C", "LUGEM2B", "LUGEM1C", "LUGEM1D", "MEDIX2B", "MEDIX1C", "MEDIX1D", "OSPEN5B", "OSPEN4C", "OSPEN3D", "RUPET2B", "RUPET2C", "RUPET2D", "SOVIL2B", "SOVIL1C", "SOVIL1D", "STEIN4B", "STEIN3C", "STEIN3D", "AGMI2C", "ASPI2C", "EMKO3C", "EWUK1C", "IMVO3C", "IRGO2C", "ODSU2C", "OSMO2C", "OTGA2C", "UMSU3C", "UNGU2C", "VABG2C", "EMKO3D", "EWUK1D", "IMVO3D", "IRGO2D", "ODSU2D", "OSMO2D", "OTGA2D", "LOWW1C" };
						if (sid129.find(sid) != sid129.end())
						{
							res.tag += "->129.050";
						}
						else if (sid125.find(sid) != sid125.end())
						{
							res.tag += "->125.175";
						}
						else if (sid134.find(sid) != sid134.end())
						{
							res.tag += "->134.675";
						}
						else
						{
							res.tag += "->" + airport->second.appFreq + "??";
						}
						return res;
					}
				}

				for (auto center : airport->second.ctrStations)
				{
					for (auto station : this->radarScreen->centerStations)
					{
						if (station.first.find(center) != std::string::npos)
						{
							res.tag += "->" + station.second;
							return res;
						}
					}
				}

				// Nothing online, UNICOM
				res.tag += "->122.8";
			}
		}

		return res;
	}

	if (!this->noChecks && rwy.empty())
	{
		res.valid = false;
		res.tag = "!RWY";
		res.color = TAG_COLOR_RED;

		return res;
	}

	EuroScopePlugIn::CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
	std::string assignedSquawk = cad.GetSquawk();
	std::string currentSquawk = rt.GetPosition().GetSquawk();

	if (this->noChecks && assignedSquawk.empty())
	{
		assignedSquawk = "2000";
	}

	if (assignedSquawk.empty())
	{
		res.valid = false;
		res.tag = "!ASSR";
		res.color = TAG_COLOR_RED;

		return res;
	}

	bool clearanceFlag = fp.GetClearenceFlag();
	if (!this->noChecks && !clearanceFlag)
	{
		res.valid = false;
		res.tag = "!CLR";
		res.color = TAG_COLOR_RED;

		return res;
	}

	if (assignedSquawk != currentSquawk)
	{
		res.tag = assignedSquawk;
		res.color = TAG_COLOR_ORANGE;
	}

	if (me.IsController() && me.GetRating() > 1 && me.GetFacility() >= 3)
	{
		if (res.tag.empty())
		{
			res.tag = "OK";
		}
		else
		{
			res.tag += "->OK";
		}

		return res;
	}

	bool groundOnline = false;
	for (auto station : this->radarScreen->groundStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			groundOnline = true;
			continue;
		}
	}

	if (groundOnline || this->groundOverride)
	{
		EuroScopePlugIn::CPosition position = rt.GetPosition().GetPosition();
		for (auto& geoGnd : airport->second.geoGndFreq)
		{
			u_int corners = geoGnd.second.lat.size();
			double lat[10], lon[10];
			std::copy(geoGnd.second.lat.begin(), geoGnd.second.lat.end(), lat);
			std::copy(geoGnd.second.lon.begin(), geoGnd.second.lon.end(), lon);

			if (CDelHelX::PointInsidePolygon(static_cast<int>(corners), lon, lat, position.m_Longitude, position.m_Latitude))
			{
				res.tag += "->" + geoGnd.second.freq;
				return res;
			}
		}

		// Didn't find any geo-based GND, so return default
		res.tag += "->" + airport->second.gndFreq;
		return res;
	}

	bool towerOnline = false;
	for (auto station : this->radarScreen->towerStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			towerOnline = true;
			continue;
		}
	}

	if (towerOnline || this->towerOverride)
	{
		for (auto rwyFreq : airport->second.rwyTwrFreq)
		{
			if (rwy == rwyFreq.first)
			{
				res.tag += "->" + rwyFreq.second;
				return res;
			}
		}

		// Didn't find a runway specific tower, so return default
		res.tag += "->" + airport->second.twrFreq;
		return res;
	}

	for (auto station : this->radarScreen->approachStations)
	{
		if (station.find(dep) != std::string::npos)
		{
			// Search for SID-specific freq
			auto sid125 = std::set<std::string>{ "BUWUT2A", "LANUX4A", "LEDVA4A", "ADAMA2B", "BUWUT2B", "", "KOXER2B", "LANUX6B", "LEDVA3B", "ADAMA1A", "ADAMA1F", "ADAMA1D", "BUWUT1E", "BUWUT1F", "BUWUT1D", "KOXER1A", "KOXER1D", "KOXER1F", "LANUX1E", "LANUX1F", "LANUX6D", "LEDVA1E", "LEDVA1F", "LEDVA4D", "LOWW1A", "LOWW1B", "LOWW1D" };
			auto sid129 = std::set<std::string>{ "ARSIN2A", "LUGEM2A", "MEDIX2A", "OSPEN3A", "RUPET2A", "SOVIL2A", "STEIN3A", "ARSIN1E", "LUGEM1E", "MEDIX1E", "OSPEN1E", "RUPET1E", "SOVIL1E", "STEIN1E", "IMVO3A", "IRGO1A", "ODSU1A", "OSMO1A" };
			auto sid134 = std::set<std::string>{ "ADAMA2C", "ARSIN1B", "ARSIN1C", "ARSIN1D", "BUWUT1C", "KOXER1C", "LANUX2C", "LEDVA3C", "LUGEM2B", "LUGEM1C", "LUGEM1D", "MEDIX2B", "MEDIX1C", "MEDIX1D", "OSPEN5B", "OSPEN4C", "OSPEN3D", "RUPET2B", "RUPET2C", "RUPET2D", "SOVIL2B", "SOVIL1C", "SOVIL1D", "STEIN4B", "STEIN3C", "STEIN3D", "AGMI2C", "ASPI2C", "EMKO3C", "EWUK1C", "IMVO3C", "IRGO2C", "ODSU2C", "OSMO2C", "OTGA2C", "UMSU3C", "UNGU2C", "VABG2C", "EMKO3D", "EWUK1D", "IMVO3D", "IRGO2D", "ODSU2D", "OSMO2D", "OTGA2D", "LOWW1C" };
			if (sid129.find(sid) != sid129.end())
			{
				res.tag += "->129.050";
			}
			else if (sid125.find(sid) != sid125.end())
			{
				res.tag += "->125.175";
			}
			else if (sid134.find(sid) != sid134.end())
			{
				res.tag += "->134.675";
			}
			else
			{
				res.tag += "->" + airport->second.appFreq + "??";
			}
			return res;
		}
	}

	for (auto center : airport->second.ctrStations)
	{
		for (auto station : this->radarScreen->centerStations)
		{
			if (station.first.find(center) != std::string::npos)
			{
				res.tag += "->" + station.second;
				return res;
			}
		}
	}

	// Nothing online, UNICOM
	res.tag += "->122.8";
	return res;
}

bool CDelHelX::PointInsidePolygon(int polyCorners, double polyX[], double polyY[], double x, double y) {
	int   i, j = polyCorners - 1;
	bool  oddNodes = false;

	for (i = 0; i < polyCorners; i++) {
		if (polyY[i] < y && polyY[j] >= y
			|| polyY[j] < y && polyY[i] >= y) {
			if (polyX[i] + (y - polyY[i]) / (polyY[j] - polyY[i]) * (polyX[j] - polyX[i]) < x) {
				oddNodes = !oddNodes;
			}
		}
		j = i;
	}

	return oddNodes;
}

void CDelHelX::LoadSettings()
{
	const char* settings = this->GetDataFromSettings(PLUGIN_NAME);
	if (settings) {
		std::vector<std::string> splitSettings = split(settings, SETTINGS_DELIMITER);

		if (splitSettings.size() < 3) {
			this->LogMessage("Invalid saved settings found, reverting to default.", "Settings");

			this->SaveSettings();

			return;
		}

		std::istringstream(splitSettings[0]) >> this->updateCheck;
		std::istringstream(splitSettings[1]) >> this->flashOnMessage;
		std::istringstream(splitSettings[2]) >> this->debug;

		this->LogMessage("Successfully loaded settings.", "Settings");
	}
	else {
		this->LogMessage("No saved settings found, using defaults.", "Settings");
	}
}

void CDelHelX::SaveSettings()
{
	std::ostringstream ss;
	ss << this->updateCheck << SETTINGS_DELIMITER
		<< this->flashOnMessage << SETTINGS_DELIMITER
		<< this->debug;

	this->SaveDataToSettings(PLUGIN_NAME, "DelHelX settings", ss.str().c_str());
}

void CDelHelX::LoadConfig()
{
	json config;
	try
	{
		std::filesystem::path base(GetPluginDirectory());
		base.append("config.json");

		std::ifstream ifs(base.c_str());

		config = json::parse(ifs);
	}
	catch (std::exception e)
	{
		this->LogMessage("Failed to read config. Error: " + std::string(e.what()), "Config");
		return;
	}

	for (auto& [icao, json_airport] : config.items())
	{
		// Get basic airport attributes
		airport ap{
			icao,
			json_airport.value<std::string>("gndFreq", ""),
			json_airport.value<std::string>("twrFreq", ""),
			json_airport.value<std::string>("appFreq", "")
		};

		auto ctrStations{ json_airport["ctrStations"].get<std::vector<std::string>>() };
		ap.ctrStations = ctrStations;

		json json_geoGnds;
		try
		{
			json_geoGnds = json_airport.at("geoGndFreq");
		}
		catch (std::exception e)
		{
			this->LogMessage("Failed to get geographic ground frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [name, json_geoGnd] : json_geoGnds.items())
		{
			geoGndFreq ggf{
				name,
				json_geoGnd.value<std::string>("freq", "")
			};

			auto lat{ json_geoGnd["lat"].get<std::vector<double>>() };
			auto lon{ json_geoGnd["lon"].get<std::vector<double>>() };
			ggf.lat = lat;
			ggf.lon = lon;

			ap.geoGndFreq.emplace(name, ggf);
		}

		json json_rwyTwrs;
		try
		{
			json_rwyTwrs = json_airport.at("rwyTwrFreq");
		}
		catch (std::exception e)
		{
			this->LogMessage("Failed to get runway tower frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [rwyid, json_rwy] : json_rwyTwrs.items())
		{
			auto rwyFreq = json_rwy.value<std::string>("freq", "");
			ap.rwyTwrFreq.emplace(rwyid, rwyFreq);
		}

		json json_taxiouts;
		try
		{
			json_taxiouts = json_airport.at("taxiOutStands");
		}
		catch (std::exception e)
		{
			this->LogMessage("Failed to get taxi out stands for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [name, json_taxiout] : json_taxiouts.items())
		{
			taxiOutStands tos{
				name
			};

			auto lat{ json_taxiout["lat"].get<std::vector<double>>() };
			auto lon{ json_taxiout["lon"].get<std::vector<double>>() };
			tos.lat = lat;
			tos.lon = lon;

			ap.taxiOutStands.emplace(name, tos);
		}

		json json_nap_reminder;
		try
		{
			json_nap_reminder = json_airport.at("napReminder");
		}
		catch (std::exception e)
		{
			this->LogMessage("Failed to load NAP reminder config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		napReminder reminder{
			json_nap_reminder.value<bool>("enabled", false),
			json_nap_reminder.value<int>("hour", 0),
			json_nap_reminder.value<int>("minute", 0),
			json_nap_reminder.value<std::string>("tzone", ""),
			false
		};
		ap.nap_reminder = reminder;


		this->airports.emplace(icao, ap);
	}

	this->LogMessage("Successfully loaded config for " + std::to_string(this->airports.size()) + " airport(s).", "Config");

	for (auto& airport : this->airports)
	{
		this->LogDebugMessage("Airport: " + airport.first, "Config");
		this->LogDebugMessage("--> GND: " + airport.second.gndFreq, "Config");
		this->LogDebugMessage("--> TWR: " + airport.second.twrFreq, "Config");
		this->LogDebugMessage("--> APP: " + airport.second.appFreq, "Config");
		int ctrIndex = 0;
		for (auto ctr : airport.second.ctrStations)
		{
			this->LogDebugMessage("--> CTR[" + std::to_string(ctrIndex) + "]: " + ctr, "Config");
			ctrIndex++;
		}
		for (auto& geoGnd : airport.second.geoGndFreq)
		{
			this->LogDebugMessage("--> GeoGnd " + geoGnd.first, "Config");
			this->LogDebugMessage("----> FRQ: " + geoGnd.second.freq, "Config");
			std::string lat_string = std::accumulate(std::begin(geoGnd.second.lat), std::end(geoGnd.second.lat), std::string(),
				[](std::string& ss, double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LAT: " + lat_string, "Config");
			std::string lon_string = std::accumulate(std::begin(geoGnd.second.lon), std::end(geoGnd.second.lon), std::string(),
				[](std::string& ss, double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LON: " + lon_string, "Config");
		}
		for (auto& twrRwy : airport.second.rwyTwrFreq)
		{
			this->LogDebugMessage("--> TWR[" + twrRwy.first + "]: " + twrRwy.second, "Config");
		}
		for (auto& taxiOut : airport.second.taxiOutStands)
		{
			this->LogDebugMessage("--> TaxiOut " + taxiOut.first, "Config");
			std::string lat_string = std::accumulate(std::begin(taxiOut.second.lat), std::end(taxiOut.second.lat), std::string(),
				[](std::string& ss, double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LAT: " + lat_string, "Config");
			std::string lon_string = std::accumulate(std::begin(taxiOut.second.lon), std::end(taxiOut.second.lon), std::string(),
				[](std::string& ss, double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LON: " + lon_string, "Config");
		}
		this->LogDebugMessage("---> NAP reminder: Enabled=" + std::to_string(airport.second.nap_reminder.enabled) + ", Hour=" + std::to_string(airport.second.nap_reminder.hour) + ", Minute=" + std::to_string(airport.second.nap_reminder.minute) + ", TZone=" + airport.second.nap_reminder.tzone, "Config");
	}

}

void CDelHelX::LogMessage(const std::string& message, const std::string& type)
{
	this->DisplayUserMessage(PLUGIN_NAME, type.c_str(), message.c_str(), true, true, true, this->flashOnMessage, false);
}

void CDelHelX::LogDebugMessage(const std::string& message, const std::string& type)
{
	if (this->debug) {
		this->LogMessage(message, type);
	}
}

void CDelHelX::OnTimer(int Counter)
{
	if (this->updateCheck && this->latestVersion.valid() && this->latestVersion.wait_for(0ms) == std::future_status::ready) {
		this->CheckForUpdate();
	}

	if (Counter > 0 && Counter % 10 == 0)
	{
		for (auto& airport : this->airports)
		{
			if (airport.second.nap_reminder.enabled && !airport.second.nap_reminder.triggered)
			{
				try
				{
					std::ostringstream timeStream;
					timeStream << date::make_zoned(airport.second.nap_reminder.tzone, std::chrono::system_clock::now());
					std::string timeString = timeStream.str();

					std::vector<std::string> timeSplit = split(timeString, ' ');
					if (timeSplit.size() == 3)
					{
						auto tod = timeSplit[1];
						std::vector<std::string> todSplit = split(tod, ':');
						if (todSplit.size() == 3)
						{
							int hours = atoi(todSplit[0].c_str());
							int minutes = atoi(todSplit[1].c_str());

							if ((hours == airport.second.nap_reminder.hour && minutes >= airport.second.nap_reminder.minute) || hours > airport.second.nap_reminder.hour)
							{
								airport.second.nap_reminder.triggered = true;

								Beep(1568, 300);
								MessageBox(nullptr, ("What's the NAP procedure for " + airport.first + " tonight?").c_str(), "DelHelX Plugin", MB_OK | MB_ICONQUESTION | MB_TOPMOST);
							}
						}
					}
				}
				catch (std::exception e)
				{
					this->LogMessage("Error processing NAP-reminder for airport " + airport.first + ". Error: " + std::string(e.what()), "Config");
				}
			}
		}
	}

	if (Counter > 0 && Counter % 2 == 0)
	{
		this->UpdateTowerSameSID();
		this->AutoUpdateDepartureHoldingPoints();
	}
}

void CDelHelX::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
	std::string callSign = FlightPlan.GetCallsign();
	if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
	{
		this->twrSameSID.RemoveFpFromTheList(FlightPlan);
		this->twrSameSID_flightPlans.erase(callSign);
	}
}

void CDelHelX::UpdateTowerSameSID()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		std::string callSign = fp.GetCallsign();

		if (!pos.IsValid() || !fp.IsValid()) {
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
			}

			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty()) {
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
			}

			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
			}

			continue;
		}

		// Check if the flight plan needs to be added to the list
		std::string groundState = fp.GetGroundState();
		auto pressAlt = pos.GetPressureAltitude();
		if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < 650 && this->twrSameSID_flightPlans.find(callSign) == this->twrSameSID_flightPlans.end())
		{
			this->twrSameSID.AddFpToTheList(fp);
			this->twrSameSID_flightPlans.emplace(callSign, 0);
		}

		// Check if we need to remove the flight plan because of ground state
		if (!(groundState == "TAXI" || groundState == "DEPA") && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			this->twrSameSID.RemoveFpFromTheList(fp);
			this->twrSameSID_flightPlans.erase(callSign);
		}

		// Check if aircraft started takeoff roll, gs>40 knots
		if (groundState == "DEPA" && pressAlt >= 650 && this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			if (this->twrSameSID_flightPlans.at(callSign) == 0)
			{
				this->twrSameSID_flightPlans[callSign] = GetTickCount();
			}
		}

		// Check if the aircraft has departed and is further than 15nm away or more than 4 minutes have passed since takeoff
		if (this->twrSameSID_flightPlans.find(callSign) != this->twrSameSID_flightPlans.end())
		{
			if (this->twrSameSID_flightPlans.at(callSign) > 0)
			{
				unsigned long now = GetTickCount();
				auto seconds = (now - this->twrSameSID_flightPlans.at(callSign)) / 1000;
				if (seconds > 4 * 60)
				{
					this->twrSameSID.RemoveFpFromTheList(fp);
					this->twrSameSID_flightPlans.erase(callSign);
					continue;
				}
			}

			EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
			std::string rwy = fpd.GetDepartureRwy();
			auto position = pos.GetPosition();
			auto distance = DistanceFromRunwayThreshold(rwy, position);

			if (distance >= 15)
			{
				this->twrSameSID.RemoveFpFromTheList(fp);
				this->twrSameSID_flightPlans.erase(callSign);
			}
		}
	}
}

void CDelHelX::AutoUpdateDepartureHoldingPoints()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
		std::string callSign = fp.GetCallsign();

		if (!pos.IsValid() || !fp.IsValid())
		{
			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty())
		{
			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			continue;
		}

		EuroScopePlugIn::CFlightPlanData fpd = fp.GetFlightPlanData();
		std::string rwy = fpd.GetDepartureRwy();
		std::string groundState = fp.GetGroundState();
		auto pressAlt = pos.GetPressureAltitude();
		auto groundSpeed = pos.GetReportedGS();

		if ((groundState == "TAXI" || groundState == "DEPA") && pressAlt < 650 && groundSpeed < 30)
		{
			if (rwy == "29")
			{
				double polyX_A1[] = { 16.575017166000304, 16.57587010846486, 16.576755237437506, 16.575939845899068 };
				double polyY_A1[] = { 48.10963587193707, 48.10939230478594, 48.11054207487674, 48.11067101945089 };
				double polyX_A2[] = { 16.574126672609637, 16.574802589279663, 16.575521421293814, 16.574850869041803 };
				double polyY_A2[] = { 48.10991525636538, 48.10969676354447, 48.11066385587193, 48.11092890762859 };
				double polyX_A3[] = { 16.5707470892528, 16.571814608437997, 16.572565626960245, 16.57168586240561 };
				double polyY_A3[] = { 48.110968307129184, 48.110710419149186, 48.1116022762453, 48.11187448657303 };
				double polyX_A4[] = { 16.564658474791777, 16.567287039619643, 16.568107795576104, 16.567292404037662 };
				double polyY_A4[] = { 48.11312806818993, 48.11213236874584, 48.11324626144721, 48.11343966800904 };
				double polyX_A6[] = { 16.55801808013691, 16.560827559183743, 16.561581956335207, 16.55922771694874 };
				double polyY_A6[] = { 48.11520333876569, 48.114222056963136, 48.11520333876569, 48.11592409133777 };
				double polyX_A8[] = { 16.548848252668353, 16.551156968088797, 16.551748779647273, 16.549999358666724 };
				double polyY_A8[] = { 48.11817746343429, 48.11736122447819, 48.118429279218226, 48.11895027347323 };

				if (CDelHelX::PointInsidePolygon(4, polyX_A1, polyY_A1, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A1");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A2, polyY_A2, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A2");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A3, polyY_A3, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A3");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A4, polyY_A4, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A4");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A6, polyY_A6, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A6");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A8, polyY_A8, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A8");
				}
			}

			if (rwy == "11")
			{
				double polyX_A12[] = { 16.533506042225728, 16.534412628870438, 16.53472912953339, 16.53421950982186 };
				double polyY_A12[] = { 48.123124385103026, 48.12283432967016, 48.12391934344905, 48.1241377757948 };
				double polyX_A11[] = { 16.534761316041482, 16.535496241309684, 16.536102420545497, 16.53547478363762 };
				double polyY_A11[] = { 48.122683929911986, 48.122476234283916, 48.12350754223812, 48.12367584400152 };
				double polyX_A10[] = { 16.539262062766852, 16.54024375126379, 16.54077482864738, 16.539878970838696 };
				double polyY_A10[] = { 48.12122646179901, 48.120932814575376, 48.122028611512825, 48.12225063288102 };
				double polyX_A9[] = { 16.544417268490587, 16.54728723212918, 16.546654230803284, 16.54508245632458 };
				double polyY_A9[] = { 48.11955766374615, 48.118680269653105, 48.12015571562148, 48.12046727282223 };
				double polyX_A7[] = { 16.5502949260553, 16.55391082964335, 16.554021387846582, 16.55095827527469 };
				double polyY_A7[] = { 48.11764919470844, 48.116524680359404, 48.11764919470844, 48.118613044560995 };


				if (CDelHelX::PointInsidePolygon(4, polyX_A12, polyY_A12, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A12");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A11, polyY_A11, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A11");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A10, polyY_A10, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A10");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A9, polyY_A9, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A9");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_A7, polyY_A7, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "A7");
				}
			}

			if (rwy == "16")
			{
				double polyX_B1[] = { 16.576179185170687, 16.577895798935828, 16.577691951051214, 16.576125540990525 };
				double polyY_B1[] = { 48.119518510356244, 48.11924992171454, 48.119837233722585, 48.12008791363267 };
				double polyX_B2[] = { 16.576806822078563, 16.578437605155443, 16.578180113090674, 16.576592245357922 };
				double polyY_B2[] = { 48.11829373475215, 48.11803230225993, 48.11870915898957, 48.11883092062948 };
				double polyX_B4[] = { 16.57980561496776, 16.58195824821892, 16.581776152354777, 16.579799111544045 };
				double polyY_B4[] = { 48.110527790794244,48.109533392525634, 48.11016303558523, 48.110940308446594 };
				double polyX_B5[] = { 16.580601483468982, 16.582357407873253, 16.582487476347644, 16.580516938960628 };
				double polyY_B5[] = { 48.10851538448417, 48.10729079094436, 48.10786400857423, 48.10909727331532 };
				double polyX_B7[] = { 16.58227286336878, 16.584191373366036, 16.584165359671157, 16.582344401029694 };
				double polyY_B7[] = { 48.104524491682014, 48.10366894985964, 48.10427260775143, 48.104928371959815 };

				if (CDelHelX::PointInsidePolygon(4, polyX_B1, polyY_B1, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B1");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B2, polyY_B2, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B2");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B4, polyY_B4, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B4");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B5, polyY_B5, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B5");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B7, polyY_B7, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B7");
				}
			}

			if (rwy == "34")
			{
				double polyX_B12[] = { 16.58845903017965, 16.591316815971858, 16.59029435236105, 16.588126729506136 };
				double polyY_B12[] = { 48.08739391988782, 48.087598820037584, 48.08936434250654, 48.08886918274589 };
				double polyX_B11[] = { 16.587344778760706, 16.590568793988357, 16.590123547293025, 16.586979998335615 };
				double polyY_B11[] = { 48.08897443323162, 48.08936859485416, 48.090328903252, 48.08997058132015 };
				double polyX_B10[] = { 16.58429723570477, 16.587451396208735, 16.586228752549466, 16.58309410231666 };
				double polyY_B10[] = { 48.096111232527555, 48.09661942262088, 48.09958159310501, 48.09920807367939 };
				double polyX_B8[] = { 16.583679410447537, 16.58554589305504, 16.5847004479715, 16.58284697221144 };
				double polyY_B8[] = { 48.10110604549481, 48.101279769383865, 48.10351641201675, 48.10326886606825 };
				double polyX_B6[] = { 16.58241774624248, 16.58376395495242, 16.58296403383492, 16.581286150515286 };
				double polyY_B6[] = { 48.10515800240615, 48.1053143416084, 48.107042726654754, 48.10670834513769 };

				if (CDelHelX::PointInsidePolygon(4, polyX_B12, polyY_B12, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B12");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B11, polyY_B11, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B11");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B10, polyY_B10, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B10");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B8, polyY_B8, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B8");
				}
				if (CDelHelX::PointInsidePolygon(4, polyX_B6, polyY_B6, pos.GetPosition().m_Longitude, pos.GetPosition().m_Latitude))
				{
					fpcad.SetFlightStripAnnotation(3, "B6");
				}
			}
		}
	}
}

double CDelHelX::DistanceFromRunwayThreshold(const std::string& rwy, const EuroScopePlugIn::CPosition& currentPosition)
{
	EuroScopePlugIn::CPosition rwyThreshold;
	if (rwy == "29")
	{
		rwyThreshold.m_Latitude = 48.109137371047005;
		rwyThreshold.m_Longitude = 16.57538568208911;
	}

	if (rwy == "11")
	{
		rwyThreshold.m_Latitude = 48.122766803767036;
		rwyThreshold.m_Longitude = 16.53361062366087;
	}

	if (rwy == "34")
	{
		rwyThreshold.m_Latitude = 48.088822783854226;
		rwyThreshold.m_Longitude = 16.5912652809662;
	}

	if (rwy == "16")
	{
		rwyThreshold.m_Latitude = 48.119602230239316;
		rwyThreshold.m_Longitude = 16.57825221198715;
	}

	return currentPosition.DistanceTo(rwyThreshold);
}

bool CDelHelX::MatchesRunwayHoldingPoint(const std::string& rwy, const std::string& hp, int index)
{
	if (rwy == "29")
	{
		if (hp.rfind("A1", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("A2", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("A3", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("A4", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A6", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A8", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "11")
	{
		if (hp.rfind("A12", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("A11", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("A10", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("A9", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("A7", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "16")
	{
		if (hp.rfind("B1", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("B2", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("B4", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("B5", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("B7", 0) == 0 && index == 4)
			return true;
	}

	if (rwy == "34")
	{
		if (hp.rfind("B12", 0) == 0 && index == 1)
			return true;
		if (hp.rfind("B11", 0) == 0 && index == 2)
			return true;
		if (hp.rfind("B10", 0) == 0 && index == 3)
			return true;
		if (hp.rfind("B8", 0) == 0 && index == 4)
			return true;
		if (hp.rfind("B6", 0) == 0 && index == 4)
			return true;
	}

	return false;
}

std::string CDelHelX::GetRunwayHoldingPoint(const std::string& rwy, int index)
{
	if (rwy == "29")
	{
		if (index == 1)
			return "A1";
		if (index == 2)
			return "A2";
		if (index == 3)
			return "A3";
	}

	if (rwy == "11")
	{
		if (index == 1)
			return "A12";
		if (index == 2)
			return "A11";
		if (index == 3)
			return "A10";
	}

	if (rwy == "16")
	{
		if (index == 1)
			return "B1";
		if (index == 2)
			return "B2";
		if (index == 3)
			return "B4";
	}

	if (rwy == "34")
	{
		if (index == 1)
			return "B12";
		if (index == 2)
			return "B11";
		if (index == 3)
			return "B10";
	}

	return "";
}

void CDelHelX::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
	std::string station = sStation;
	to_upper(station);

	this->LogDebugMessage("New METAR for station " + station + ": " + sFullMetar, "Metar");

	auto airport = this->airports.find(station);
	if (airport == this->airports.end())
	{
		// Station not in airport config, so ignore it
		return;
	}

	std::vector<std::string> metarElements = split(sFullMetar);
	for (std::string metarElement : metarElements)
	{
		static const std::regex qnh(R"(Q[0-9]{4})");
		static const std::regex alt(R"(A[0-9]{4})");

		if (std::regex_match(metarElement, qnh) || std::regex_match(metarElement, alt))
		{
			// Check if existing QNH and if that is now different
			auto existingQNH = this->airportQNH.find(station);
			if (existingQNH == this->airportQNH.end())
			{
				this->LogDebugMessage("First QNH value for airport " + station + " is " + metarElement, "Metar");

				// No existing QNH, add it
				this->airportQNH.emplace(station, metarElement);
			}
			else
			{
				if (existingQNH->second != metarElement)
				{
					this->LogDebugMessage("New QNH value for airport " + station + " is " + metarElement, "Metar");

					// Save new QNH
					this->airportQNH[station] = metarElement;

					// Set flight strip annotation on aircraft on the ground at that airport
					for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
						EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();

						// Skip aircraft is not on the ground
						// TODO better option for finding aircraft on ground??? maybe airport elevation via config???
						if (!pos.IsValid() || pos.GetReportedGS() > 40) {
							continue;
						}

						EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
						// Skip aircraft is tracked (with exception of aircraft tracked by current controller)
						if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
							continue;
						}

						std::string dep = fp.GetFlightPlanData().GetOrigin();
						to_upper(dep);

						if (dep == station && fp.GetClearenceFlag())
						{
							EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
							fpcad.SetFlightStripAnnotation(2, "NQNH");
						}
					}
				}
			}
		}
	}
}

void CDelHelX::CheckForUpdate()
{
	try
	{
		semver::version latest{ this->latestVersion.get() };
		semver::version current{ PLUGIN_VERSION };

		if (latest > current) {
			std::ostringstream ss;
			ss << "A new version (" << latest << ") of " << PLUGIN_NAME << " is available, download it at " << PLUGIN_LATEST_DOWNLOAD_URL;

			this->LogMessage(ss.str(), "Update");
		}
	}
	catch (std::exception& e)
	{
		MessageBox(NULL, e.what(), PLUGIN_NAME, MB_OK | MB_ICONERROR);
	}

	this->latestVersion = std::future<std::string>();
}

void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	*ppPlugInInstance = pPlugin = new CDelHelX();
}

void __declspec (dllexport) EuroScopePlugInExit(void)
{
	delete pPlugin;
}