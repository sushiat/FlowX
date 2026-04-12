// EuroScopePlugIn.h — Test stub shadowing the real EuroScope SDK header.
// Provides simple data-holder types so FlowX source files compile without
// a live EuroScope installation.  All SDK methods are no-ops or return safe
// defaults; tests configure data by setting public member variables directly.

#pragma once

#include <string>
#include <map>

// ─── Suppress the real SDK's dllimport declarations ──────────────────────────
#ifndef DllSpecEuroScope
#define DllSpecEuroScope
#define ESINDEX void *
#endif

// ─── Verbatim constants from the real header (unchanged) ─────────────────────
namespace EuroScopePlugIn
{

const int COMPATIBILITY_CODE                           = 16;

const int FLIGHT_PLAN_STATE_NOT_STARTED                = 0;
const int FLIGHT_PLAN_STATE_SIMULATED                  = 1;
const int FLIGHT_PLAN_STATE_TERMINATED                 = 2;

const int FLIGHT_PLAN_STATE_NON_CONCERNED              = 0;
const int FLIGHT_PLAN_STATE_NOTIFIED                   = 1;
const int FLIGHT_PLAN_STATE_COORDINATED                = 2;
const int FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED   = 3;
const int FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED = 4;
const int FLIGHT_PLAN_STATE_ASSUMED                    = 5;
const int FLIGHT_PLAN_STATE_REDUNDANT                  = 7;

const int TAG_COLOR_DEFAULT                            = 0;
const int TAG_COLOR_RGB_DEFINED                        = 1;

const int CONNECTION_TYPE_NO                           = 0;
const int CONNECTION_TYPE_DIRECT                       = 1;
const int CONNECTION_TYPE_VIA_PROXY                    = 2;
const int CONNECTION_TYPE_SIMULATOR_SERVER             = 3;
const int CONNECTION_TYPE_PLAYBACK                     = 4;
const int CONNECTION_TYPE_SIMULATOR_CLIENT             = 5;
const int CONNECTION_TYPE_SWEATBOX                     = 6;

const int SECTOR_ELEMENT_INFO                          =  0;
const int SECTOR_ELEMENT_AIRPORT                       =  3;
const int SECTOR_ELEMENT_RUNWAY                        =  4;
const int SECTOR_ELEMENT_SID                           =  7;

const int TAG_ITEM_FUNCTION_NO                         = 0;
const int TAG_ITEM_FUNCTION_SET_CLEARED_FLAG           = 27;
const int TAG_ITEM_FUNCTION_SET_GROUND_STATUS          = 28;

const int BUTTON_LEFT                                  = 1;
const int BUTTON_MIDDLE                                = 2;
const int BUTTON_RIGHT                                 = 3;

const int POPUP_ELEMENT_UNCHECKED                      = 0;
const int POPUP_ELEMENT_CHECKED                        = 1;
const int POPUP_ELEMENT_NO_CHECKBOX                    = 2;

// ─── CPosition ───────────────────────────────────────────────────────────────

struct CPosition
{
    double m_Latitude  = 0.0;
    double m_Longitude = 0.0;

    CPosition() = default;

    double DistanceTo(const CPosition& other) const
    {
        // Haversine in NM — good enough for stubs
        const double R  = 3440.065; // NM
        const double PI = 3.14159265358979323846;
        double dLat = (other.m_Latitude  - m_Latitude)  * PI / 180.0;
        double dLon = (other.m_Longitude - m_Longitude) * PI / 180.0;
        double a    = sin(dLat / 2) * sin(dLat / 2)
                    + cos(m_Latitude * PI / 180.0) * cos(other.m_Latitude * PI / 180.0)
                    * sin(dLon / 2) * sin(dLon / 2);
        return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    }

    double DirectionTo(const CPosition& other) const
    {
        const double PI = 3.14159265358979323846;
        double dLon = (other.m_Longitude - m_Longitude) * PI / 180.0;
        double lat1 = m_Latitude        * PI / 180.0;
        double lat2 = other.m_Latitude  * PI / 180.0;
        double y    = sin(dLon) * cos(lat2);
        double x    = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
        double brg  = atan2(y, x) * 180.0 / PI;
        return fmod(brg + 360.0, 360.0);
    }
};

// ─── CSectorElement ──────────────────────────────────────────────────────────

struct CSectorElement
{
    bool        valid           = false;
    std::string airportName;
    std::string name;
    std::string runwayActive1;  // active designator for runway end 1
    std::string runwayActive2;  // active designator for runway end 2
    bool        runway1IsDep    = false;
    bool        runway2IsDep    = false;

    bool        IsValid()           const { return valid; }
    const char* GetAirportName()    const { return airportName.c_str(); }
    const char* GetName()           const { return name.c_str(); }
    bool        IsElementActive(bool dep, int end = 0) const
    {
        return end == 0 ? runway1IsDep == dep : runway2IsDep == dep;
    }
    const char* GetRunwayName(int end) const
    {
        return end == 0 ? runwayActive1.c_str() : runwayActive2.c_str();
    }
};

// ─── CFlightPlanData ─────────────────────────────────────────────────────────

struct CFlightPlanData
{
    std::string origin;
    std::string destination;
    std::string departureRwy;
    std::string sidName;
    std::string planType      = "I";
    std::string aircraftInfo;
    char        aircraftWtc   = 'M';

    const char* GetOrigin()          const { return origin.c_str(); }
    const char* GetDestination()     const { return destination.c_str(); }
    const char* GetDepartureRwy()    const { return departureRwy.c_str(); }
    const char* GetSidName()         const { return sidName.c_str(); }
    const char* GetPlanType()        const { return planType.c_str(); }
    const char* GetAircraftInfo()    const { return aircraftInfo.c_str(); }
    const char* GetAircraftFPType()  const { return aircraftInfo.c_str(); }
    const char* GetRoute()           const { return ""; }
    char        GetAircraftWtc()     const { return aircraftWtc; }
    bool        AmendFlightPlan()          { return true; }
    bool        SetPlanType(const char*)   { return true; }
};

// ─── CFlightPlanControllerAssignedData ───────────────────────────────────────

struct CFlightPlanControllerAssignedData
{
    std::string squawk;
    std::string annotations[9]; // slots 0–8
    std::string scratchpad;
    int         clearedAlt      = 0;
    std::string groundState;
    int         commType        = 0;

    const char* GetSquawk()                       const { return squawk.c_str(); }
    const char* GetFlightStripAnnotation(int slot) const
    {
        if (slot >= 0 && slot < 9) return annotations[slot].c_str();
        return "";
    }
    const char* GetScratchPadString()             const { return scratchpad.c_str(); }
    int         GetClearedAltitude()              const { return clearedAlt; }
    int         GetCommunicationType()            const { return commType; }

    bool SetFlightStripAnnotation(int slot, const char* v)
    {
        if (slot >= 0 && slot < 9) { annotations[slot] = v ? v : ""; return true; }
        return false;
    }
    bool SetSquawk(const char* v)         { squawk = v ? v : ""; return true; }
    bool SetScratchPadString(const char* v) { scratchpad = v ? v : ""; return true; }
    bool SetClearedAltitude(int v)        { clearedAlt = v; return true; }
    bool SetGroundState(const char* v)    { groundState = v ? v : ""; return true; }
    bool SetCommunicationType(int v)      { commType = v; return true; }
};

// ─── CRadarTargetPositionData ─────────────────────────────────────────────────

struct CRadarTargetPositionData
{
    bool        valid_         = false;
    CPosition   position_;
    std::string squawk_;
    int         gs_            = 0;
    int         pressureAlt_   = 0;
    int         flightLevel_   = 0;
    int         heading_       = 0;
    bool        transponderC_  = false;

    bool        IsValid()          const { return valid_; }
    CPosition   GetPosition()      const { return position_; }
    const char* GetSquawk()        const { return squawk_.c_str(); }
    int         GetReportedGS()    const { return gs_; }
    int         GetPressureAltitude() const { return pressureAlt_; }
    int         GetFlightLevel()   const { return flightLevel_; }
    int         GetReportedHeading() const { return heading_; }
    int         GetReportedHeadingTrueNorth() const { return heading_; }
    bool        GetTransponderC()  const { return transponderC_; }
    bool        IsFPTrackPosition() const { return false; }
    int         GetReceivedTime()  const { return 0; }
};

// ─── CFlightPlan ─────────────────────────────────────────────────────────────

struct CFlightPlan
{
    bool                               valid_         = false;
    std::string                        callsign_;
    std::string                        groundState_;
    bool                               clearanceFlag_ = false;
    bool                               trackingMe_    = false;
    CFlightPlanData                    fpData_;
    CFlightPlanControllerAssignedData  assignedData_;
    CRadarTargetPositionData           posData_;
    int                                fpState_       = 0;

    bool        IsValid()          const { return valid_; }
    const char* GetCallsign()      const { return callsign_.c_str(); }
    const char* GetGroundState()   const { return groundState_.c_str(); }
    bool        GetClearenceFlag() const { return clearanceFlag_; }
    bool        GetTrackingControllerIsMe() const { return trackingMe_; }
    int         GetFPState()       const { return fpState_; }

    CFlightPlanData GetFlightPlanData() const { return fpData_; }
    CFlightPlanControllerAssignedData GetControllerAssignedData() const { return assignedData_; }
    CRadarTargetPositionData GetFPTrackPosition() const { return posData_; }

    bool        PushFlightStrip(const char*)                                { return true; }
    bool        InitiateCoordination(const char*, const char*, const char*) { return true; }
    bool        EndTracking()                                               { return true; }
    bool        StartTracking()                                             { return true; }
    const char* GetPilotName()                                              const { return ""; }
};

// ─── CRadarTarget ─────────────────────────────────────────────────────────────

struct CRadarTarget
{
    bool                     valid_    = false;
    std::string              callsign_;
    CRadarTargetPositionData posData_;
    CFlightPlan              correlatedFP_;

    bool        IsValid()   const { return valid_; }
    const char* GetCallsign() const { return callsign_.c_str(); }
    CRadarTargetPositionData GetPosition() const { return posData_; }
    CFlightPlan GetCorrelatedFlightPlan() const { return correlatedFP_; }
};

// ─── CController ─────────────────────────────────────────────────────────────

struct CController
{
    bool        valid_        = false;
    bool        isController_ = false;
    std::string callsign_;
    int         facility_     = 0;
    int         rating_       = 0;
    double      primaryFreq_  = 0.0;

    bool        IsValid()           const { return valid_; }
    bool        IsController()      const { return isController_; }
    const char* GetCallsign()       const { return callsign_.c_str(); }
    int         GetFacility()       const { return facility_; }
    int         GetRating()         const { return rating_; }
    double      GetPrimaryFrequency() const { return primaryFreq_; }
};

// ─── CFlightPlanList ─────────────────────────────────────────────────────────

struct CFlightPlanList
{
    // No-op placeholder; needed for CFlowX_Base member declarations.
    void AddFPListColumn(const char*, int, int, bool, bool, const char*, int) {}
    void SetColumnData(const char*, const char*, int) {}
    void Sort(int, bool) {}
    void AddFpToTheList(CFlightPlan) {}
    void RemoveFpFromTheList(CFlightPlan) {}
};

// ─── CRadarScreen base ───────────────────────────────────────────────────────

class CRadarScreen
{
  public:
    virtual ~CRadarScreen() = default;

    // No-op overrideable stubs for methods RadarScreen overrides
    virtual void OnRefresh(HDC, int) {}
    virtual void OnClickScreenObject(int, const char*, POINT, RECT, int) {}
    virtual void OnButtonDownScreenObject(int, const char*, POINT, RECT, int) {}
    virtual void OnButtonUpScreenObject(int, const char*, POINT, RECT, int) {}
    virtual void OnMoveScreenObject(int, const char*, POINT, RECT, bool) {}
    virtual void OnOverScreenObject(int, const char*, POINT, RECT) {}
    virtual void OnFunctionCall(int, const char*, POINT, RECT) {}
    virtual void OnControllerDisconnect(CController) {}
    virtual void OnControllerPositionUpdate(CController) {}
    virtual void OnDoubleClickScreenObject(int, const char*, POINT, RECT, int) {}
    virtual void OnFlightPlanDisconnect(CFlightPlan) {}
    virtual void OnRadarTargetPositionUpdate(CRadarTarget) {}
    virtual void OnAsrContentLoaded(bool) {}
    virtual void OnAsrContentToBeClosed() {}
    virtual void OnAsrContentToBeSaved() {}

    void AddScreenObject(int, const char*, RECT, bool, const char*) {}
    void RequestRefresh() {}
    CFlightPlan GetPlugIn_CFlightPlan(const char*) const { return CFlightPlan{}; }
    const char* GetDataFromAsr(const char*) const { return nullptr; }
    void SaveDataToAsr(const char*, const char*, const char*) {}
    HWND GetHWND() { return nullptr; }
};

// ─── CPlugIn base ─────────────────────────────────────────────────────────────

// Configurable global controller returned by ControllerMyself().
// Tests set this directly before constructing the accessor.
inline CController& TestControllerMyself()
{
    static CController c;
    return c;
}

class CPlugIn
{
  public:
    CPlugIn(int, const char*, const char*, const char*, const char*) {}
    virtual ~CPlugIn() = default;

    // Registration — all no-ops
    void RegisterTagItemType(const char*, int) {}
    void RegisterTagItemFunction(const char*, int) {}
    void RegisterDisplayType(const char*, bool, bool, bool, bool) {}
    void DisplayUserMessage(const char*, const char*, const char*, bool, bool, bool, bool, bool) {}
    void OpenPopupList(RECT, const char*, int) {}
    void AddPopupListElement(const char*, const char*, int, bool, int, bool, bool) {}

    // Controller iteration — returns invalid (empty) controllers
    CController ControllerMyself() const { return TestControllerMyself(); }
    CController ControllerSelectFirst() const { return CController{}; }
    CController ControllerSelectNext(const CController&) const { return CController{}; }

    // Flight plan iteration — returns invalid plans
    CFlightPlan FlightPlanSelectFirst() const { return CFlightPlan{}; }
    CFlightPlan FlightPlanSelectNext(const CFlightPlan&) const { return CFlightPlan{}; }

    // Radar target iteration — returns invalid targets
    CRadarTarget RadarTargetSelectFirst() const { return CRadarTarget{}; }
    CRadarTarget RadarTargetSelectNext(const CRadarTarget&) const { return CRadarTarget{}; }

    // Sector file — returns invalid element (stops iteration immediately)
    CSectorElement SectorFileElementSelectFirst(int) const { return CSectorElement{}; }
    CSectorElement SectorFileElementSelectNext(const CSectorElement&, int) const { return CSectorElement{}; }

    // Radar screen creation — virtual so CFlowX_Base can override it
    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char*, bool, bool, bool, bool) { return nullptr; }

    // Flight-plan list helpers
    CFlightPlanList AddFPList(const char*) { return CFlightPlanList{}; }

    // Misc
    int         GetConnectionType() const { return 0; }
    double      GetMagneticVariation() const { return 0.0; }
    const char* GetTransitionAltitude() const { return ""; }
    bool        StartTagFunction(const char*, const char*, int, const char*, const char*, int, POINT) { return false; }
};

} // namespace EuroScopePlugIn
