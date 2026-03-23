#pragma once

#include <map>
#include <set>
#include <string>

#include "constants.h"
#include "EuroScope/EuroScopePlugIn.h"

/// @brief Per-aircraft departure information rendered as an on-screen overlay near the radar target.
struct depInfo
{
    std::string dep_info = std::string(""); ///< Departure status text (SID, T-flag, etc.)
    POINT pos = { -1,-1 };                  ///< Screen position anchor (set from radar target pixel position)
    COLORREF dep_color = TAG_COLOR_TURQ;    ///< Colour of the departure info text
    POINT lastDrag = { -1,-1 };             ///< Previous drag position; { -1,-1 } when not dragging
    int dragX = 0;                          ///< Accumulated horizontal drag offset in pixels
    int dragY = 0;                          ///< Accumulated vertical drag offset in pixels
    std::string hp_info = std::string(""); ///< Holding-point name to display below the dep_info text
    COLORREF hp_color = TAG_COLOR_TURQ;    ///< Colour of the holding-point text
    COLORREF sid_color = TAG_COLOR_TURQ;   ///< Colour of the SID indicator dot drawn below the text
};

/// @brief EuroScope radar screen implementation; handles rendering, controller tracking, and drag interaction.
class RadarScreen : public EuroScopePlugIn::CRadarScreen
{
public:
    /// @brief Constructs a RadarScreen with debug mode off.
    RadarScreen();

    /// @brief Default destructor.
    virtual ~RadarScreen();

    bool debug; ///< When true, controller connect/disconnect events are logged to the chat window

    /// @brief Callsign -> primary frequency string for online GND controllers (facility 3).
    std::map<std::string, std::string> groundStations;
    /// @brief Callsign -> primary frequency string for online TWR controllers (facility 4, excluding ATIS).
    std::map<std::string, std::string> towerStations;
    /// @brief Callsign -> primary frequency string for online APP controllers (facility 5).
    std::map<std::string, std::string> approachStations;
    /// @brief Callsign -> primary frequency string for online CTR controllers (facility 6).
    std::map<std::string, std::string> centerStations;

    /// @brief Callsign -> departure overlay data for aircraft currently shown on the radar.
    std::map<std::string, depInfo> radarTargetDepartureInfos;

    /// @brief Called by EuroScope when the ASR is closed; notifies the plugin and deletes this screen.
    void OnAsrContentToBeClosed() override;

private:
    /// @brief Called when a controller's position is updated; adds the controller to the appropriate station set.
    /// @param Controller The updated controller object.
    void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;

    /// @brief Called when a controller disconnects; removes the controller from the appropriate station set.
    /// @param Controller The disconnected controller object.
    void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;

    /// @brief Called each radar refresh cycle; draws departure info overlays after all tags are rendered.
    /// @param hDC Device context for GDI drawing.
    /// @param Phase EuroScope refresh phase constant.
    /// @note Only draws during REFRESH_PHASE_AFTER_TAGS.
    void OnRefresh(HDC hDC, int Phase) override;

    /// @brief Called when a radar target's position changes; updates the screen-pixel anchor for the overlay.
    /// @param RadarTarget The updated radar target.
    void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;

    /// @brief Called when a flight plan disconnects; removes the aircraft's departure overlay.
    /// @param FlightPlan The disconnecting flight plan.
    void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;

    /// @brief Called when the controller drags an on-screen object; accumulates drag offsets for overlay repositioning.
    /// @param ObjectType Type identifier of the dragged screen object.
    /// @param sObjectId String identifier of the dragged object (callsign for departure overlays).
    /// @param Pt Current cursor position during the drag.
    /// @param Area Bounding rectangle of the object.
    /// @param Released True when the mouse button has been released.
    void OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released) override;
};
