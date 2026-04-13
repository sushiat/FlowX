/**
 * @file RadarScreen.h
 * @brief Declaration of RadarScreen and the depInfo overlay struct.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "constants.h"
#include "DifliModel.h"
#include "EuroScope/EuroScopePlugIn.h"
#include "PopoutWindow.h"
#include "cachedTagData.h"
#include "taxi_graph.h"

class CFlowX_Settings; ///< Forward declaration — avoid circular include between RadarScreen.h and CFlowX_Settings.h

/// @brief Per-aircraft departure information rendered as an on-screen overlay near the radar target.
struct depInfo
{
    std::string dep_info  = std::string(""); ///< Departure status text (SID, T-flag, etc.)
    GeoPoint    anchor    = {0.0, 0.0};      ///< Geographic position of the radar target; pixels recomputed each frame so panning works
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
    PopoutWindow* currentPopout_       = nullptr;        ///< Set during RenderToPopout; used by AddScreenObjectAuto to route hit-area registration
    HWND          esHwnd_              = nullptr;        ///< EuroScope radar screen HWND; cached on first OnRefresh for use in Create*Popout
    bool          isPopoutRender_      = false;          ///< True while a draw function renders into a memory DC for a popout window
    POINT         popoutHoverPoint_    = {-9999, -9999}; ///< Cursor position inside the active popout window; {-9999,-9999} when outside
    ULONGLONG     taxiSafetyLastTickMs = 0;              ///< GetTickCount64 timestamp of last UpdateTaxiSafety() run; throttle guard.

    /// @brief Routes an AddScreenObject call to the active popout (isPopoutRender_) or to EuroScope (normal render).
    void AddScreenObjectAuto(int objectType, const char* objectId, RECT rect, bool dragable,
                             const char* tooltip);

    /// @brief Renders a draw function into a memory DC and pushes the bitmap to a popout window.
    void RenderToPopout(HDC screenDC, PopoutWindow* popout, POINT& windowPos, int w, int h,
                        std::function<void(HDC)> drawFn);

    /// @brief Creates the Approach Estimate popout window, seeding position/size from settings or in-screen state.
    void CreateApproachEstPopout(CFlowX_Settings* s);

    /// @brief Creates the DEP/H popout window, seeding position/size from settings or in-screen state.
    void CreateDepRatePopout(CFlowX_Settings* s);

    /// @brief Creates the DIFLIS popout window, seeding position/size from settings or in-screen state.
    void CreateDifliPopout(CFlowX_Settings* s);

    /// @brief Creates the WX/ATIS popout window, seeding position/size from settings or in-screen state.
    void CreateWeatherPopout(CFlowX_Settings* s);

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

    /// @brief Detects ALT keypress each BEFORE_TAGS frame and toggles taxiSwingoverActive when it fires.
    /// Computes the fixed swingover segment (taxiSwingoverFixedSeg / taxiSwingoverOrigin) on activation.
    /// No-op when not in non-push taxi planning mode.
    void UpdateSwingoverState();
    void RecalculateTaxiPreview();

    /// @brief Draws the OSM taxiway/taxilane polylines, derived runway centrelines, holding-position
    /// circles, and way labels. Gated on showTaxiOverlay / showTaxiLabels.
    void DrawTaxiOverlay(HDC hDC);

    /// @brief Draws a single TaxiRoute polyline with the given GDI pen colour and width.
    /// No-op when route.valid is false or polyline is empty.
    void DrawRoutePolyline(HDC hDC, const TaxiRoute& route, COLORREF col, int width);

    /// @brief Draws confirmed (2-second flash) and persistently tracked taxi routes.
    /// Prunes stale entries from taxiAssigned and completed routes from taxiTracked.
    void DrawTaxiRoutes(HDC hDC);

    /// @brief Draws active push routes, the yellow suggested route, and the magenta/light-blue
    /// planning preview including via-point markers and dead-end warnings.
    void DrawPlanningRoutes(HDC hDC);

    /// @brief Draws the Approach Estimate window showing inbound TTT on a vertical time bar, split by runway estimateBarSide.
    void DrawApproachEstimateWindow(HDC hDC);

    /// @brief Draws departure info overlays (text, SID dot, HP label, connector line).
    void DrawDepartureInfoTag(HDC hDC);

    /// @brief Draws a green square near each aircraft in gndTransferSquares and registers it as a clickable screen object.
    void DrawGndTransferSquares(HDC hDC);

    /// @brief Draws the DEP/H departure-rate window using pre-calculated depRateRowsCache.
    void DrawDepRateWindow(HDC hDC);

    /// @brief Draws the DIFLIS (Digital Flight Strip) window from the pre-computed difliStripsCache
    /// and the primary airport's DifliAirportConfig group definitions. Works for both in-screen and popout paths.
    void DrawDifliWindow(HDC hDC);

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

    /// @brief Highlights dead-end taxilane branches that are blocked by active push routes in red.
    /// Called only during taxi planning mode for a non-push aircraft.
    void DrawPushDeadEnds(HDC hDC);

    /// @brief Draws all graph nodes and directed edges as a diagnostic overlay.
    void DrawTaxiGraph(HDC hDC);

    /// @brief Draws the TWR Inbound custom window using pre-calculated twrInboundRowsCache.
    void DrawTwrInbound(HDC hDC);

    /// @brief Draws the TWR Outbound custom window using pre-calculated twrOutboundRowsCache.
    void DrawTwrOutbound(HDC hDC);

    /// @brief Draws the WX/ATIS window showing wind, QNH, and ATIS letter per configured airport.
    void DrawWeatherWindow(HDC hDC);

  public:
    POINT                                         approachEstLastDrag = {-1, -1};       ///< Previous drag cursor position for the Approach Estimate window; (-1,-1) when not dragging
    std::unique_ptr<PopoutWindow>                 approachEstPopout;                    ///< Non-null when the Approach Estimate window is in standalone popout mode
    POINT                                         approachEstResizeLastDrag = {-1, -1}; ///< Previous drag cursor position for the resize handle; (-1,-1) when not dragging
    POINT                                         approachEstWindowPos      = {-1, -1}; ///< Top-left corner of the Approach Estimate window; (-1,-1) until first draw (auto-positioned)
    int                                           approachEstWindowH        = 380;      ///< Current height of the Approach Estimate window in pixels
    int                                           approachEstWindowW        = 260;      ///< Current width of the Approach Estimate window in pixels
    std::map<std::string, std::string>            approachStations;                     ///< Callsign -> primary frequency string for online APP controllers (facility 5)
    std::map<std::string, std::string>            centerStations;                       ///< Callsign -> primary frequency string for online CTR controllers (facility 6)
    bool                                          debug;                                ///< When true, controller connect/disconnect events are logged to the chat window
    POINT                                         depRateLastDrag = {-1, -1};           ///< Previous drag cursor position for the departure rate window; (-1,-1) when not dragging
    std::map<std::string, std::vector<ULONGLONG>> depRateLog;                           ///< Runway designator -> list of takeoff timestamps (GetTickCount64 ms) used for per-hour departure rate counting
    std::unique_ptr<PopoutWindow>                 depRatePopout;                        ///< Non-null when the DEP/H window is in standalone popout mode
    std::vector<DepRateRowCache>                  depRateRowsCache;                     ///< Cached per-runway rows for the DEP/H window; rebuilt every second by UpdateTagCache()
    int                                           depRateWindowH   = 0;                 ///< Last-rendered height of the DEP/H window in pixels; 0 until first draw
    int                                           depRateWindowW   = 0;                 ///< Last-rendered width of the DEP/H window in pixels; 0 until first draw
    POINT                                         depRateWindowPos = {-1, -1};          ///< Top-left corner of the departure rate window; (-1,-1) until first draw (auto-positioned to lower-right)
    POINT                                         difliLastDrag       = {-1, -1};       ///< Previous drag cursor position for the DIFLIS window; (-1,-1) when not dragging
    std::unique_ptr<PopoutWindow>                 difliPopout;                          ///< Non-null when the DIFLIS window is in standalone popout mode
    POINT                                         difliResizeLastDrag = {-1, -1};       ///< Previous drag cursor position for the DIFLIS resize handle
    std::vector<DifliStripCache>                  difliStripsCache;                     ///< Cached flight strips rebuilt every second by UpdateTagCache()
    std::map<std::string, std::string>            difliOverrides;                       ///< Callsign -> manually-forced group id (persists until auto-state explicitly clears it)
    std::deque<DifliUndoEntry>                    difliUndoStack;                       ///< Bounded manual-move undo stack for the DIFLIS window (cap 32)
    int                                           difliWindowH   = 720;                 ///< Current height of the DIFLIS window in pixels
    int                                           difliWindowW   = 1100;                ///< Current width of the DIFLIS window in pixels
    POINT                                         difliWindowPos = {-1, -1};            ///< Top-left corner of the DIFLIS window; (-1,-1) until first draw
    std::set<std::string>                         gndTransferSquares;                   ///< Callsigns for which a GND-transfer green square is currently shown on the radar
    std::map<std::string, ULONGLONG>              gndTransferSquareTimes;               ///< Tick (GetTickCount64 ms) when each callsign's GND-transfer square first appeared; used to age-colour the square.
    std::map<std::string, std::string>            groundStations;                       ///< Callsign -> primary frequency string for online GND controllers (facility 3)
    std::string                                   hoveredTaxiTarget;                    ///< Callsign of the radar target currently hovered by the mouse (if it has a tracked route); cleared when stale
    ULONGLONG                                     hoveredTaxiTargetTick = 0;            ///< GetTickCount64 tick when hoveredTaxiTarget was last refreshed by OnOverScreenObject
    ULONGLONG                                     napAckClickTick       = 0;            ///< Tick (GetTickCount64) at which the ACK button was clicked; 0 when not animating
    bool                                          napAckPressed         = false;        ///< True while the left mouse button is held down over the ACK button
    POINT                                         napLastDrag           = {-1, -1};     ///< Previous drag cursor position for the NAP reminder window
    int                                           napLastHoverType      = -1;           ///< Last object type reported by OnOverScreenObject for NAP objects; used to detect enter/leave transitions
    bool                                          napReminderActive     = false;        ///< True while the NAP reminder window should be visible
    std::string                                   napReminderAirport;                   ///< ICAO code of the airport whose NAP reminder is currently active
    POINT                                         napWindowPos = {-1, -1};              ///< Top-left corner of the NAP reminder window; (-1,-1) until first shown (auto-centred)
    std::map<std::string, depInfo>                radarTargetDepartureInfos;            ///< Callsign -> departure overlay data for aircraft currently shown on the radar
    bool                                          logTaxiTests           = false;       ///< When true, assigned taxi routes are logged as JSON test case templates
    bool                                          showTaxiGraph          = false;       ///< When true, the routing graph nodes and edges are drawn as a diagnostic overlay
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
        std::string csA, csB;   ///< Callsigns of the two conflicting aircraft.
        GeoPoint    pt;         ///< Geographic intersection point.
        double      tA   = 0.0; ///< Seconds until csA reaches pt (from last recompute).
        double      tB   = 0.0; ///< Seconds until csB reaches pt (from last recompute).
        size_t      segA = 0;   ///< Index of the B-end node of the conflicting segment in csA's polyline.
        size_t      segB = 0;   ///< Index of the B-end node of the conflicting segment in csB's polyline.
    };

    std::map<std::string, ULONGLONG>   taxiConflictFirstSeen;             ///< Keys "csA|csB" (sorted) -> GetTickCount64 tick when the conflict first entered the <15 s window; cleared when it exits.
    std::set<std::string>              taxiConflictSoundPlayed;           ///< Keys "csA|csB" (sorted) for conflicts where the <15 s sound has already fired; cleared when conflict resolves.
    std::map<std::string, TaxiRoute>   pushTracked;                       ///< Callsign -> assigned pushback route; used to block taxi routing and show orange overlay
    std::map<std::string, TaxiRoute>   taxiAssigned;                      ///< Callsign -> controller-confirmed taxi route (green); auto-removed 2 s after assignment
    std::map<std::string, TaxiRoute>   taxiTracked;                       ///< Callsign -> persistent taxi route for "Show routes" display; cleared on disconnect or re-assignment
    std::map<std::string, ULONGLONG>   taxiAssignedTimes;                 ///< Tick (GetTickCount64 ms) when each confirmed taxi route was last assigned
    std::map<std::string, GeoPoint>    taxiAssignedPos;                   ///< Aircraft position at the moment each taxi route was confirmed; deviation warning is suppressed until the position changes by > 5 m (i.e. a fresh position update has been received)
    std::vector<TaxiConflictInfo>      taxiConflicts;                     ///< Active taxi path conflicts; recomputed every ~250 ms by UpdateTaxiSafety()
    GeoPoint                           taxiCursorSnap;                    ///< Current snapped cursor geo-position; updated every OnRefresh frame during planning mode
    std::set<std::string>              taxiDeviations;                    ///< Callsigns of moving aircraft currently off their assigned route (GS > 3 kt, dist > 60 m)
    TaxiRoute                          taxiGreenPreview;                  ///< Current green preview route recomputed each frame from origin through waypoints to cursor snap
    POINT                              taxiOriginPx = {-1, -1};           ///< Screen pixel where right-click activated planning; used for accept-suggestion proximity test
    std::string                        taxiPlanActive;                    ///< Callsign currently being planned; empty when not in planning mode
    bool                               taxiPlanForwardOnly   = false;     ///< True when aircraft is in a taxiOutStand/taxiOnlyZone; suppresses backward start candidates
    bool                               taxiPlanIsPush        = false;     ///< True when the active planning session is a pushback (no suggestion; endpoint only)
    double                             taxiPushHeading       = 0.0;       ///< Aircraft true-north heading captured at push planning activation; used to compute the reservation zone.
    GeoPoint                           taxiPushOrigin        = {};        ///< Current push-zone pivot (snapped taxiway node); updated on mouse move when cursor crosses a taxiway midpoint.
    GeoPoint                           taxiPushStandOrigin   = {};        ///< Aircraft geo-position at push activation; used to project cursor onto push axis for pivot selection.
    double                             taxiPushWingspan      = 0.0;       ///< Aircraft wingspan (m) captured at push activation; used for taxiway restriction filtering.
    bool                               taxiAltPrevDown       = false;     ///< Previous-frame ALT key state; used to detect press edges for swingover toggle.
    TaxiRoute                          taxiSwingoverFixedSeg = {};        ///< Fixed route segment origin→crossPt→s-bend→partnerPt; computed when swingover is toggled ON.
    GeoPoint                           taxiSwingoverOrigin   = {};        ///< Partner-lane snap point where free routing begins after the swingover crossover.
    double                             taxiSwingoverBearing  = -1.0;      ///< Forward bearing on the partner lane at the swingover origin; passed to FindWaypointRoute so WayRefOnEdge can lock onto the correct lane.
    bool                               taxiSwingoverActive   = false;     ///< Toggled by an ALT keypress during taxi planning; routes via a fixed s-bend crossover to the partner taxilane.
    std::map<std::string, TaxiRoute>   taxiSuggested;                     ///< Callsign -> auto-calculated suggested route (yellow); computed on planning activation
    std::vector<GeoPoint>              taxiWaypoints;                     ///< Mandatory via-points added by middle-click; route recalculated through all in order
    bool                               taxiMidDrawing = false;            ///< True while the middle mouse button is held during taxi planning (draw gesture in progress).
    std::vector<GeoPoint>              taxiDrawPolyline;                  ///< Subsampled cursor positions collected during a middle-drag gesture; rendered as cyan intent line; cleared on button release.
    std::set<int>                      taxiDrawnNodeSet;                  ///< Graph node IDs collected during a middle-drag gesture; passed to FindWaypointRoute as preferredNodes to bias routing toward the drawn path.
    GeoPoint                           taxiLastDrawnPos = {};             ///< Last position at which a draw sample was taken; used to throttle sampling by distance.
    std::map<std::string, std::string> towerStations;                     ///< Callsign -> primary frequency string for online TWR controllers (facility 4, excluding ATIS)
    POINT                              twrInboundLastDrag = {-1, -1};     ///< Previous drag cursor position for the TWR Inbound window
    std::vector<TwrInboundRowCache>    twrInboundRowsCache;               ///< Cached per-aircraft rows for the TWR Inbound window; rebuilt every second (and on position updates)
    int                                twrInboundWindowH   = 0;           ///< Last-rendered height of the TWR Inbound window in pixels; 0 until first draw
    int                                twrInboundWindowW   = 0;           ///< Last-rendered width of the TWR Inbound window in pixels; 0 until first draw
    POINT                              twrInboundWindowPos = {-1, -1};    ///< Top-left corner of the TWR Inbound window; (-1,-1) until first draw
    POINT                              twrOutboundLastDrag = {-1, -1};    ///< Previous drag cursor position for the TWR Outbound window
    std::vector<TwrOutboundRowCache>   twrOutboundRowsCache;              ///< Cached per-aircraft rows for the TWR Outbound window; rebuilt every second by UpdateTagCache()
    int                                twrOutboundWindowH     = 0;        ///< Last-rendered height of the TWR Outbound window in pixels; 0 until first draw
    int                                twrOutboundWindowW     = 0;        ///< Last-rendered width of the TWR Outbound window in pixels; 0 until first draw
    POINT                              twrOutboundWindowPos   = {-1, -1}; ///< Top-left corner of the TWR Outbound window; (-1,-1) until first draw
    int                                winCloseLastHoverType  = -1;       ///< Last object type reported by OnOverScreenObject for window close buttons; used to detect enter/leave transitions
    int                                winPopoutLastHoverType = -1;       ///< Last object type reported by OnOverScreenObject for window popout buttons; used to detect enter/leave transitions
    POINT                              weatherLastDrag        = {-1, -1}; ///< Previous drag cursor position for the WX/ATIS window
    std::unique_ptr<PopoutWindow>      weatherPopout;                     ///< Non-null when the WX/ATIS window is in standalone popout mode
    std::vector<WeatherRowCache>       weatherRowsCache;                  ///< Cached per-airport rows for the WX/ATIS window; rebuilt every second by UpdateTagCache()
    int                                weatherWindowH   = 0;              ///< Last-rendered height of the WX/ATIS window in pixels; 0 until first draw
    int                                weatherWindowW   = 0;              ///< Last-rendered width of the WX/ATIS window in pixels; 0 until first draw
    POINT                              weatherWindowPos = {-1, -1};       ///< Top-left corner of the WX/ATIS window; (-1,-1) until first draw

    /// @brief Constructs a RadarScreen with debug mode off.
    RadarScreen();

    /// @brief Default destructor.
    virtual ~RadarScreen();

    /// @brief Called by EuroScope when the ASR is closed; notifies the plugin and deletes this screen.
    void OnAsrContentToBeClosed() override;
};
