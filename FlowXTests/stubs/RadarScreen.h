// RadarScreen.h — Test stub shadowing the real FlowX/RadarScreen.h.
// Provides the minimal RadarScreen class needed by CFlowX_Base/Tags without
// pulling in PopoutWindow, cachedTagData, or other rendering-only headers.

#pragma once

#include <map>
#include <string>
#include "EuroScope/EuroScopePlugIn.h"

class CFlowX_Settings; // forward declaration (mirrors real header)

/// @brief Per-aircraft departure info (minimal stub — rendering fields omitted).
struct depInfo
{
    std::string dep_info;
    std::string hp_info;
    int         queue_pos = 0;
};

/// @brief Minimal RadarScreen stub: exposes the station maps used by the tag layer.
class RadarScreen : public EuroScopePlugIn::CRadarScreen
{
  public:
    bool debug = false;

    // Station maps accessed by GetPushStartHelperTag
    std::map<std::string, std::string> groundStations;
    std::map<std::string, std::string> towerStations;
    std::map<std::string, std::string> approachStations;
    std::map<std::string, std::string> centerStations;

    // Departure info overlay map (accessed by UpdateRadarTargetDepartureInfo)
    std::map<std::string, depInfo> depInfoMap;

    RadarScreen()  = default;
    ~RadarScreen() = default;
};
