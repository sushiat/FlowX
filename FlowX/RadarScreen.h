/**
 * @file RadarScreen.h
 * @brief Declaration of RadarScreen and the depInfo overlay struct.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "constants.h"
#include "EuroScope/EuroScopePlugIn.h"
#include "cachedTagData.h"
#include "taxi_graph.h"

/// @brief Per-aircraft departure information rendered as an on-screen overlay near the radar target.
struct depInfo
{
    std::string dep_info  = std::string(""); ///< Departure status text (SID, T-flag, etc.)
    POINT       pos       = {-1, -1};        ///< Screen position anchor (set from radar target pixel position)
    COLORREF    dep_color = TAG_COLOR_TURQ;  ///< Colour of the departure info text
    int         queue_pos = 0;               ///< Departure queue position (1-based); 0 = not queued, drawn above the radar target.
    POINT       lastDrag  = {-1, -1};        ///< Previous drag position; { -1,-1 } when not dragging
    int         dragX     = 0;               ///< Accumulated horizontal drag offset in pixels
    int         dragY     = 0;               ///< Accumulated vertical drag offset in pixels
    std::string hp_info   = std::string(""); ///< Holding-point name to display below the dep_info text
    COLORREF    hp_color  = TAG_COLOR_TURQ;  ///< Colour of the holding-point text
    COLORREF    sid_color = TAG_COLOR_TURQ;  ///< Colour of the SID indicator dot drawn below the text
};

/// @brief EuroScope radar screen implementation; handles rendering, controller tracking, and drag interaction.
class RadarScreen : public EuroScopePlugIn::CRadarScreen
{
  private:
    ULONGLONG taxiSafetyLastTickMs = 0; ///< GetTickCount64 timestamp of last UpdateTaxiSafety() run; throttle guard.

    /// @brief Called when a mouse button is pressed on a registered screen object; sets the pressed state.
    void OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called when a mouse button is released on a registered screen object; clears the pressed state.
    void OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called when the mouse button is clicked on a registered screen object; handles ACK button.
    void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

    /// @brief Called when a controller disconnects; removes the controller from the appropriate station set.
    /// @param Controller The disconnected controller object.
    void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;

    /// @brief Called when a controller's position is updated; adds the controller to the appropriate station set.
    /// @param Controller The updated controller object.
    void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;

    /// @brief Called on a double-click on a registered screen object; handles STS column revert-to-taxi.
    void OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button) override;

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

    /// @brief Called when the mouse moves over a registered screen object; tracks ACK button hover state.
    void OnOverScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area) override;

    /// @brief Called when a radar target's position changes; updates the screen-pixel anchor for the overlay.
    /// @param RadarTarget The updated radar target.
    void OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget) override;

    /// @brief Called each radar refresh cycle; draws departure info overlays after all tags are rendered.
    /// @param hDC Device context for GDI drawing.
    /// @param Phase EuroScope refresh phase constant.
    /// @note Only draws during REFRESH_PHASE_AFTER_TAGS.
    void OnRefresh(HDC hDC, int Phase) override;

    /// @brief Recomputes taxiDeviations and taxiConflicts; throttled to 250 ms via taxiSafetyLastTickMs.
    void UpdateTaxiSafety();

    /// @brief Draws the Approach Estimate window showing inbound TTT on a vertical time bar, split by runway estimateBarSide.
    void DrawApproachEstimateWindow(HDC hDC);

    /// @brief Draws departure info overlays (text, SID dot, HP label, connector line).
    void DrawDepartureInfoTag(HDC hDC);

    /// @brief Draws a green square near each aircraft in gndTransferSquares and registers it as a clickable screen object.
    void DrawGndTransferSquares(HDC hDC);

    /// @brief Draws the DEP/H departure-rate window using pre-calculated depRateRowsCache.
    void DrawDepRateWindow(HDC hDC);

    /// @brief Draws the NAP reminder window when napReminderActive is true.
    void DrawNapReminder(HDC hDC);

    /// @brief Draws the Start button anchored to the lower-right corner, re-reading the clip box each frame.
    void DrawStartButton(HDC hDC);

    /// @brief Draws the popup menu that opens above the Start button when startMenuOpen is true.
    void DrawStartMenu(HDC hDC);

    /// @brief Redraws the conflicting polyline segments in red and places a marker at each intersection.
    void DrawTaxiConflicts(HDC hDC);

    /// @brief Draws "!route" (yellow) or "conflict" (red) warning labels above radar targets.
    void DrawTaxiWarningLabels(HDC hDC);

    /// @brief Draws the TWR Inbound custom window using pre-calculated twrInboundRowsCache.
    void DrawTwrInbound(HDC hDC);

    /// @brief Draws the TWR Outbound custom window using pre-calculated twrOutboundRowsCache.
    void DrawTwrOutbound(HDC hDC);

    /// @brief Draws the WX/ATIS window showing wind, QNH, and ATIS letter per configured airport.
    void DrawWeatherWindow(HDC hDC);

  public:
    POINT                                         approachEstLastDrag       = {-1, -1}; ///< Previous drag cursor position for the Approach Estimate window; (-1,-1) when not dragging
    POINT                                         approachEstResizeLastDrag = {-1, -1}; ///< Previous drag cursor position for the resize handle; (-1,-1) when not dragging
    POINT                                         approachEstWindowPos      = {-1, -1}; ///< Top-left corner of the Approach Estimate window; (-1,-1) until first draw (auto-positioned)
    int                                           approachEstWindowH        = 380;      ///< Current height of the Approach Estimate window in pixels
    int                                           approachEstWindowW        = 260;      ///< Current width of the Approach Estimate window in pixels
    std::map<std::string, std::string>            approachStations;                     ///< Callsign -> primary frequency string for online APP controllers (facility 5)
    std::map<std::string, std::string>            centerStations;                       ///< Callsign -> primary frequency string for online CTR controllers (facility 6)
    bool                                          debug;                                ///< When true, controller connect/disconnect events are logged to the chat window
    POINT                                         depRateLastDrag = {-1, -1};           ///< Previous drag cursor position for the departure rate window; (-1,-1) when not dragging
    std::map<std::string, std::vector<ULONGLONG>> depRateLog;                           ///< Runway designator -> list of takeoff timestamps (GetTickCount64 ms) used for per-hour departure rate counting
    std::vector<DepRateRowCache>                  depRateRowsCache;                     ///< Cached per-runway rows for the DEP/H window; rebuilt every second by UpdateTagCache()
    POINT                                         depRateWindowPos = {-1, -1};          ///< Top-left corner of the departure rate window; (-1,-1) until first draw (auto-positioned to lower-right)
    std::set<std::string>                         gndTransferSquares;                   ///< Callsigns for which a GND-transfer green square is currently shown on the radar
    std::map<std::string, ULONGLONG>              gndTransferSquareTimes;               ///< Tick (GetTickCount64 ms) when each callsign's GND-transfer square first appeared; used to age-colour the square.
    std::map<std::string, std::string>            groundStations;                       ///< Callsign -> primary frequency string for online GND controllers (facility 3)
    ULONGLONG                                     napAckClickTick   = 0;                ///< Tick (GetTickCount64) at which the ACK button was clicked; 0 when not animating
    bool                                          napAckPressed     = false;            ///< True while the left mouse button is held down over the ACK button
    POINT                                         napLastDrag       = {-1, -1};         ///< Previous drag cursor position for the NAP reminder window
    int                                           napLastHoverType  = -1;               ///< Last object type reported by OnOverScreenObject for NAP objects; used to detect enter/leave transitions
    bool                                          napReminderActive = false;            ///< True while the NAP reminder window should be visible
    std::string                                   napReminderAirport;                   ///< ICAO code of the airport whose NAP reminder is currently active
    POINT                                         napWindowPos = {-1, -1};              ///< Top-left corner of the NAP reminder window; (-1,-1) until first shown (auto-centred)
    std::map<std::string, depInfo>                radarTargetDepartureInfos;            ///< Callsign -> departure overlay data for aircraft currently shown on the radar
    bool                                          showTaxiLabels         = false;       ///< When true, taxiway name labels are drawn over the TAXI network overlay
    bool                                          showTaxiOverlay        = false;       ///< When true, taxiway/taxilane geometry from osmData is drawn on the radar screen as a debug overlay
    bool                                          showTaxiRoutes         = false;       ///< When true, all assigned taxi routes are drawn persistently, clipped to the remaining portion
    int                                           startBtnLastHoverType  = -1;          ///< Last object type reported by OnOverScreenObject for the Start button; used to detect hover transitions
    bool                                          startBtnPressed        = false;       ///< True while the left mouse button is held down over the Start button
    int                                           startMenuLastHoverType = -1;          ///< Last object type reported by OnOverScreenObject for Start menu items; used to detect hover transitions
    bool                                          startMenuOpen          = false;       ///< True while the Start button popup menu is visible
    /// @brief A predicted taxi path intersection between two aircraft with conflicting routes.
    struct TaxiConflictInfo
    {
        std::string csA, csB; ///< Callsigns of the two conflicting aircraft.
        GeoPoint    pt;       ///< Geographic intersection point.
        double      tA = 0.0; ///< Seconds until csA reaches pt (from last recompute).
        double      tB = 0.0; ///< Seconds until csB reaches pt (from last recompute).
        size_t      segA = 0; ///< Index of the B-end node of the conflicting segment in csA's polyline.
        size_t      segB = 0; ///< Index of the B-end node of the conflicting segment in csB's polyline.
    };

    std::map<std::string, TaxiRoute>              taxiAssigned;                         ///< Callsign -> controller-confirmed taxi route (green); auto-removed 2 s after assignment
    std::map<std::string, TaxiRoute>              taxiTracked;                          ///< Callsign -> persistent taxi route for "Show routes" display; cleared on disconnect or re-assignment
    std::map<std::string, ULONGLONG>              taxiAssignedTimes;                    ///< Tick (GetTickCount64 ms) when each confirmed taxi route was last assigned
    std::vector<TaxiConflictInfo>                 taxiConflicts;                        ///< Active taxi path conflicts; recomputed every ~250 ms by UpdateTaxiSafety()
    GeoPoint                                      taxiCursorSnap;                       ///< Current snapped cursor geo-position; updated every OnRefresh frame during planning mode
    std::set<std::string>                         taxiDeviations;                       ///< Callsigns of moving aircraft currently off their assigned route (GS > 3 kt, dist > 60 m)
    TaxiRoute                                     taxiGreenPreview;                     ///< Current green preview route recomputed each frame from origin through waypoints to cursor snap
    POINT                                         taxiOriginPx = {-1, -1};              ///< Screen pixel where right-click activated planning; used for accept-suggestion proximity test
    std::string                                   taxiPlanActive;                       ///< Callsign currently being planned; empty when not in planning mode
    std::map<std::string, TaxiRoute>              taxiSuggested;                        ///< Callsign -> auto-calculated suggested route (yellow); computed on planning activation
    std::vector<GeoPoint>                         taxiWaypoints;                        ///< Mandatory via-points added by middle-click; route recalculated through all in order
    std::map<std::string, std::string>            towerStations;                        ///< Callsign -> primary frequency string for online TWR controllers (facility 4, excluding ATIS)
    POINT                                         twrInboundLastDrag = {-1, -1};        ///< Previous drag cursor position for the TWR Inbound window
    std::vector<TwrInboundRowCache>               twrInboundRowsCache;                  ///< Cached per-aircraft rows for the TWR Inbound window; rebuilt every second (and on position updates)
    POINT                                         twrInboundWindowPos = {-1, -1};       ///< Top-left corner of the TWR Inbound window; (-1,-1) until first draw
    POINT                                         twrOutboundLastDrag = {-1, -1};       ///< Previous drag cursor position for the TWR Outbound window
    std::vector<TwrOutboundRowCache>              twrOutboundRowsCache;                 ///< Cached per-aircraft rows for the TWR Outbound window; rebuilt every second by UpdateTagCache()
    POINT                                         twrOutboundWindowPos  = {-1, -1};     ///< Top-left corner of the TWR Outbound window; (-1,-1) until first draw
    int                                           winCloseLastHoverType = -1;           ///< Last object type reported by OnOverScreenObject for window close buttons; used to detect enter/leave transitions
    POINT                                         weatherLastDrag       = {-1, -1};     ///< Previous drag cursor position for the WX/ATIS window
    std::vector<WeatherRowCache>                  weatherRowsCache;                     ///< Cached per-airport rows for the WX/ATIS window; rebuilt every second by UpdateTagCache()
    POINT                                         weatherWindowPos = {-1, -1};          ///< Top-left corner of the WX/ATIS window; (-1,-1) until first draw

    /// @brief Constructs a RadarScreen with debug mode off.
    RadarScreen();

    /// @brief Default destructor.
    virtual ~RadarScreen();

    /// @brief Called by EuroScope when the ASR is closed; notifies the plugin and deletes this screen.
    void OnAsrContentToBeClosed() override;
};
