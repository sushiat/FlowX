#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "constants.h"
#include "EuroScope/EuroScopePlugIn.h"
#include "cachedTagData.h"

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

    /// @brief Runway designator -> list of takeoff timestamps (GetTickCount64 ms) used for per-hour departure rate counting.
    std::map<std::string, std::vector<ULONGLONG>> depRateLog;

    /// @brief Top-left corner of the departure rate window; (-1,-1) until first draw (auto-positioned to lower-right).
    POINT depRateWindowPos = { -1, -1 };

    /// @brief Previous drag cursor position for the departure rate window; (-1,-1) when not dragging.
    POINT depRateLastDrag = { -1, -1 };

    /// @brief Cached per-runway rows for the DEP/H window; rebuilt every second by UpdateTagCache().
    std::vector<DepRateRowCache> depRateRowsCache;

    /// @brief Cached per-aircraft rows for the TWR Outbound window; rebuilt every second by UpdateTagCache().
    std::vector<TwrOutboundRowCache> twrOutboundRowsCache;

    /// @brief Cached per-aircraft rows for the TWR Inbound window; rebuilt every second (and on position updates).
    std::vector<TwrInboundRowCache> twrInboundRowsCache;

    /// @brief Cached per-airport rows for the WX/ATIS window; rebuilt every second by UpdateTagCache().
    std::vector<WeatherRowCache> weatherRowsCache;

    /// @brief Top-left corner of the WX/ATIS window; (-1,-1) until first draw.
    POINT weatherWindowPos = { -1, -1 };

    /// @brief Previous drag cursor position for the WX/ATIS window.
    POINT weatherLastDrag = { -1, -1 };

    /// @brief Top-left corner of the TWR Outbound window; (-1,-1) until first draw.
    POINT twrOutboundWindowPos = { -1, -1 };

    /// @brief Previous drag cursor position for the TWR Outbound window.
    POINT twrOutboundLastDrag = { -1, -1 };

    /// @brief Top-left corner of the TWR Inbound window; (-1,-1) until first draw.
    POINT twrInboundWindowPos = { -1, -1 };

    /// @brief Previous drag cursor position for the TWR Inbound window.
    POINT twrInboundLastDrag = { -1, -1 };

    /// @brief Top-left corner of the NAP reminder window; (-1,-1) until first shown (auto-centred).
    POINT napWindowPos = { -1, -1 };

    /// @brief Previous drag cursor position for the NAP reminder window.
    POINT napLastDrag = { -1, -1 };

    /// @brief True while the NAP reminder window should be visible.
    bool napReminderActive = false;

    /// @brief ICAO code of the airport whose NAP reminder is currently active.
    std::string napReminderAirport;

    /// @brief Last object type reported by OnOverScreenObject for NAP objects; used to detect enter/leave transitions.
    int napLastHoverType = -1;

    /// @brief True while the left mouse button is held down over the ACK button.
    bool napAckPressed = false;

    /// @brief Tick (GetTickCount64) at which the ACK button was clicked; 0 when not animating.
    ULONGLONG napAckClickTick = 0;

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

    /// @brief Draws departure info overlays (text, SID dot, HP label, connector line).
    void DrawDepartureInfoTag(HDC hDC);

    /// @brief Draws the DEP/H departure-rate window using pre-calculated depRateRowsCache.
    void DrawDepRateWindow(HDC hDC);

    /// @brief Draws the TWR Outbound custom window using pre-calculated twrOutboundRowsCache.
    void DrawTwrOutbound(HDC hDC);

    /// @brief Draws the TWR Inbound custom window using pre-calculated twrInboundRowsCache.
    void DrawTwrInbound(HDC hDC);

    /// @brief Draws the NAP reminder window when napReminderActive is true.
    void DrawNapReminder(HDC hDC);

    /// @brief Draws the WX/ATIS window showing wind, QNH, and ATIS letter per configured airport.
    void DrawWeatherWindow(HDC hDC);

    /// @brief Called when the mouse moves over a registered screen object; tracks ACK button hover state.
    void OnOverScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area) override;

    /// @brief Called when a mouse button is pressed on a registered screen object; sets the pressed state.
    void OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called when a mouse button is released on a registered screen object; clears the pressed state.
    void OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called when the mouse button is clicked on a registered screen object; handles ACK button.
    void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called on a double-click on a registered screen object; handles STS column revert-to-taxi.
    void OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;
};
