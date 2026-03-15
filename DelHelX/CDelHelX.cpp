#include "pch.h"
#include "CDelHelX.h"

#include <fstream>
#include <iostream>

#include "helpers.h"
#include "date/tz.h"

CDelHelX::CDelHelX()
{
	this->groundOverride = false;
	this->towerOverride = false;
	this->noChecks = false;
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
			if (this->radarScreen != nullptr)
			{
				this->radarScreen->debug = this->debug;
			}

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
	}

	return false;
}

void CDelHelX::RedoFlags()
{
	for (EuroScopePlugIn::CRadarTarget rt = this->RadarTargetSelectFirst(); rt.IsValid(); rt = this->RadarTargetSelectNext(rt)) {
		EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
		// Skip if aircraft is not on the ground (currently using ground speed threshold)
		if (!pos.IsValid() || pos.GetReportedGS() > 40) {
			continue;
		}

		EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
		// Skip if aircraft is tracked (except for aircraft tracked by current controller)
		if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
			continue;
		}

		std::string dep = fp.GetFlightPlanData().GetOrigin();
		to_upper(dep);

		std::string arr = fp.GetFlightPlanData().GetDestination();
		to_upper(arr);

		std::string cs = fp.GetCallsign();

		// Skip aircraft without a valid flight plan (no departure/destination airport)
		if (dep.empty() || arr.empty()) {
			continue;
		}

		auto airport = this->airports.find(dep);
		if (airport == this->airports.end())
		{
			// Airport not in config
			continue;
		}

		int depElevation = airport->second.fieldElevation;
		if (pos.GetPressureAltitude() >= depElevation + 50) {
			continue;
		}

		if (fp.GetClearenceFlag() && this->radarScreen != nullptr)
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

	tagInfo tag;

	if (ItemCode == TAG_ITEM_PS_HELPER)
	{
		tag = this->GetPushStartHelperTag(FlightPlan, RadarTarget);
	}
	else if (ItemCode == TAG_ITEM_TAXIOUT)
	{
		tag = this->GetTaxiOutTag(FlightPlan, RadarTarget);
	}
	else if (ItemCode == TAG_ITEM_NEWQNH)
	{
		tag = this->GetNewQnhTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_SAMESID)
	{
		tag = this->GetSameSidTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_TIMER)
	{
		tag = this->GetTakeoffTimerTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_TAKEOFF_DISTANCE)
	{
		tag = this->GetTakeoffDistanceTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_ASSIGNED_RUNWAY)
	{
		tag = this->GetAssignedRunwayTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_TTT)
	{
		tag = this->GetTttTag(FlightPlan, RadarTarget);
	}
	else if (ItemCode == TAG_ITEM_INBOUND_NM)
	{
		tag = this->GetInboundNmTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_SUGGESTED_VACATE)
	{
		tag = this->GetSuggestedVacateTag(FlightPlan);
	}
	else if (ItemCode == TAG_ITEM_HP1)
	{
		tag = this->GetHoldingPointTag(FlightPlan, 1);
	}
	else if (ItemCode == TAG_ITEM_HP2)
	{
		tag = this->GetHoldingPointTag(FlightPlan, 2);
	}
	else if (ItemCode == TAG_ITEM_HP3)
	{
		tag = this->GetHoldingPointTag(FlightPlan, 3);
	}
	else if (ItemCode == TAG_ITEM_HPO)
	{
		tag = this->GetHoldingPointTag(FlightPlan, 4);
	}
	else if (ItemCode == TAG_ITEM_DEPARTURE_INFO)
	{
		tag = this->GetDepartureInfoTag(FlightPlan, RadarTarget);
	}

	*pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
	strcpy_s(sItemString, 16, tag.tag.c_str());
	*pRGB = tag.color;
}

void CDelHelX::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
	EuroScopePlugIn::CFlightPlan fp = this->FlightPlanSelectASEL();
	if (!fp.IsValid())
		return;

	std::string dep = fp.GetFlightPlanData().GetOrigin();
	to_upper(dep);

	static const std::vector<int> noDepartureAirportCheckRequired = { TAG_FUNC_CLRD_TO_LAND, TAG_FUNC_MISSED_APP, TAG_FUNC_STAND_AUTO };
	if (this->airports.find(dep) == this->airports.end() 
		&& std::find(noDepartureAirportCheckRequired.begin(), noDepartureAirportCheckRequired.end(), FunctionId) == noDepartureAirportCheckRequired.end())
		return;

	EuroScopePlugIn::CRadarTarget rt = fp.GetCorrelatedRadarTarget();

	if (FunctionId == TAG_FUNC_ON_FREQ)             this->Func_OnFreq(fp, rt);
	else if (FunctionId == TAG_FUNC_CLEAR_NEWQNH)   this->Func_ClearNewQnh(fp);
	else if (FunctionId == TAG_FUNC_ASSIGN_HP1)     this->Func_AssignHp(fp, 1);
	else if (FunctionId == TAG_FUNC_ASSIGN_HP2)     this->Func_AssignHp(fp, 2);
	else if (FunctionId == TAG_FUNC_ASSIGN_HP3)     this->Func_AssignHp(fp, 3);
	else if (FunctionId == TAG_FUNC_REQUEST_HP1)    this->Func_RequestHp(fp, 1);
	else if (FunctionId == TAG_FUNC_REQUEST_HP2)    this->Func_RequestHp(fp, 2);
	else if (FunctionId == TAG_FUNC_REQUEST_HP3)    this->Func_RequestHp(fp, 3);
	else if (FunctionId == TAG_FUNC_ASSIGN_HPO)     this->Func_AssignHpo(fp, Pt);
	else if (FunctionId == TAG_FUNC_REQUEST_HPO)    this->Func_RequestHpo(fp, Pt);
	else if (FunctionId == TAG_FUNC_HPO_LISTSELECT) this->Func_HpoListselect(fp, sItemString);
	else if (FunctionId == TAG_FUNC_LINE_UP)        Func_LineUp(fp);
	else if (FunctionId == TAG_FUNC_TAKE_OFF)       Func_TakeOff(fp);
	else if (FunctionId == TAG_FUNC_TRANSFER_NEXT)  this->Func_TransferNext(fp);
	else if (FunctionId == TAG_FUNC_CLRD_TO_LAND)   Func_ClrdToLand(fp, this->radarScreen);
	else if (FunctionId == TAG_FUNC_MISSED_APP)     Func_MissedApp(fp, this->radarScreen);
	else if (FunctionId == TAG_FUNC_STAND_AUTO)     Func_StandAuto(fp, this->radarScreen);
}

void CDelHelX::OnTimer(int Counter)
{
	if (this->updateCheck && this->latestVersion.valid() && this->latestVersion.wait_for(0ms) == std::future_status::ready) {
		this->CheckForUpdate();
	}

	if (Counter > 0 && Counter % 10 == 0)
	{
		this->CheckAirportNAPReminder();
	}

	if (Counter > 0 && Counter % 2 == 0)
	{
		this->UpdateTowerSameSID();
		this->AutoUpdateDepartureHoldingPoints();
		this->UpdateRadarTargetDepartureInfo();
		this->UpdateTTTInbounds();
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
						auto stationAp = this->airports.find(station);
						int stationElevation = stationAp != this->airports.end() ? stationAp->second.fieldElevation : 0;
						if (!pos.IsValid() || pos.GetPressureAltitude() >= stationElevation + 50) {
							continue;
						}

						EuroScopePlugIn::CFlightPlan fp = rt.GetCorrelatedFlightPlan();
						// Skip aircraft is tracked (except aircraft tracked by current controller)
						if (!fp.IsValid() || (strcmp(fp.GetTrackingControllerId(), "") != 0 && !fp.GetTrackingControllerIsMe())) {
							continue;
						}

						std::string callSign = fp.GetCallsign();
						std::string dep = fp.GetFlightPlanData().GetOrigin();
						to_upper(dep);

						if (dep == station && fp.GetClearenceFlag())
						{
							EuroScopePlugIn::CFlightPlanControllerAssignedData fpcad = fp.GetControllerAssignedData();
							if (this->flightStripAnnotation[callSign].empty())
							{
								this->flightStripAnnotation[callSign].append("Q");
							}
							else
							{
								this->flightStripAnnotation[callSign][0] = 'Q';
							}
							fpcad.SetFlightStripAnnotation(8, this->flightStripAnnotation[callSign].c_str());
							this->PushToOtherControllers(fp);
						}
					}
				}
			}
		}
	}
}

void CDelHelX::OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int dataType)
{
	if (dataType == EuroScopePlugIn::CTR_DATA_TYPE_SCRATCH_PAD_STRING)
	{
		std::string callSign = fp.GetCallsign();

		std::string scratch = fp.GetControllerAssignedData().GetScratchPadString();
		size_t pos = scratch.find("GRP/S/");
		if (pos != std::string::npos) {
			std::string stand = scratch.substr(pos + 6);
			
			this->standAssignment[callSign] = stand;
		}
	}
}

static CDelHelX* pPlugin;

void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	*ppPlugInInstance = pPlugin = new CDelHelX();
}

void __declspec (dllexport) EuroScopePlugInExit(void)
{
	delete pPlugin;
}