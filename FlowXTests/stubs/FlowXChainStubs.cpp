// FlowXChainStubs.cpp — Stub implementations of the FlowX class chain methods
// that are NOT compiled from production source in the test project.
//
// CFlowX_Settings.cpp is compiled from real source (to enable file-loading tests).
// Everything else in the chain (Base, Logging, Timers) is stubbed here as empty
// no-ops so the linker is satisfied without pulling in MFC or EuroScope DLL imports.

#include "pch.h"
#include "CFlowX_Tags.h"
#include "helpers.h"
#include "constants.h"

// ─── CFlowX_Base ─────────────────────────────────────────────────────────────

CFlowX_Base::CFlowX_Base()
    : EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
                                PLUGIN_NAME,
                                PLUGIN_VERSION,
                                PLUGIN_AUTHOR,
                                PLUGIN_LICENSE)
{
    this->debug       = false;
    this->radarScreen = nullptr;
}

EuroScopePlugIn::CRadarScreen* CFlowX_Base::OnRadarScreenCreated(
    const char*, bool, bool, bool, bool)
{
    return nullptr; // No radar screen in test environment
}

void CFlowX_Base::PushToOtherControllers(EuroScopePlugIn::CFlightPlan&) const {}

// ─── CFlowX_Logging ──────────────────────────────────────────────────────────

CFlowX_Logging::CFlowX_Logging()
{
    this->flashOnMessage = false;
}

void CFlowX_Logging::LogDebugSessionStart() {}
void CFlowX_Logging::LogDebugFileOnly(const std::string&, const std::string&) {}
void CFlowX_Logging::LogException(const std::string&, const std::string&) {}
void CFlowX_Logging::LogMessage(const std::string&, const std::string&) {}
void CFlowX_Logging::LogDebugMessage(const std::string&, const std::string&) {}

// ─── HTTP helpers (declared in helpers.h, not compiled from helpers.cpp) ─────

std::string FetchLatestVersion()                                            { return ""; }
std::string FetchVatsimData()                                               { return ""; }
std::map<std::string, std::string> FetchAtisData(std::vector<std::string>) { return {}; }

// ─── CFlowX_Timers ───────────────────────────────────────────────────────────

void CFlowX_Timers::CheckAirportNAPReminder()       {}
void CFlowX_Timers::CheckArrivedAtStand()           {}
void CFlowX_Timers::CheckReconnects()               {}
void CFlowX_Timers::DetectTakeoffState(EuroScopePlugIn::CRadarTarget) {}
void CFlowX_Timers::RecordDepartureSpacingSnapshot(const std::string&) {}
void CFlowX_Timers::PollAtisLetters(int)            {}
void CFlowX_Timers::UpdateAdesCache()               {}
void CFlowX_Timers::UpdateOccupiedStands()          {}
void CFlowX_Timers::UpdateRadarTargetDepartureInfo() {}
void CFlowX_Timers::UpdateTWROutbound()             {}
void CFlowX_Timers::UpdateTWRInbound()              {}
void CFlowX_Timers::AckNapReminder()                {}
void CFlowX_Timers::SaveWindowPositions()           {}
void CFlowX_Timers::AckWeather(const std::string&)  {}
void CFlowX_Timers::ClearGndTransfer(const std::string&) {}
