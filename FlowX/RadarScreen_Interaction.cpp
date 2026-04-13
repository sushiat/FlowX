/**
 * @file RadarScreen_Interaction.cpp
 * @brief RadarScreen partial implementation: mouse and screen-object interaction handlers.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "RadarScreen.h"
#include "CFlowX_Base.h"
#include "CFlowX_CustomTags.h"
#include "CFlowX_Functions.h"
#include "CFlowX_Settings.h"
#include "CFlowX_Timers.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <mmsystem.h>
#include "constants.h"
#include "helpers.h"
#include "osm_taxiways.h"
#include "taxi_graph.h"

// Forward declarations of the static helpers defined in RadarScreen_Taxi.cpp.
GeoPoint  PushZonePoint(const GeoPoint& origin, double bearingDeg, double distM);
TaxiRoute BuildPushZone(const TaxiGraph& graph, const GeoPoint& pivot,
                        double armABrng, double armADistM,
                        double armBBrng, double armBDistM,
                        double wingspanM = 0.0);

/// @brief Calls RequestRefresh() only on enter/leave transitions between NAP screen objects so that
/// DrawNapReminder() redraws exactly when the hover state changes, not on every mouse-move event.
void RadarScreen::OnOverScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area)
{
    try
    {
        if (ObjectType == SCREEN_OBJECT_NAP_WIN || ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            if (ObjectType != this->napLastHoverType)
            {
                this->napLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            if (ObjectType != this->startBtnLastHoverType)
            {
                this->startBtnLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_START_MENU_ITEM)
        {
            if (ObjectType != this->startMenuLastHoverType)
            {
                this->startMenuLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_WIN_CLOSE)
        {
            if (ObjectType != this->winCloseLastHoverType)
            {
                this->winCloseLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        if (ObjectType == SCREEN_OBJECT_WIN_POPOUT)
        {
            if (ObjectType != this->winPopoutLastHoverType)
            {
                this->winPopoutLastHoverType = ObjectType;
                this->RequestRefresh();
            }
        }

        // Hover over a ground aircraft with a tracked route: show that route individually.
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET &&
            (this->taxiTracked.count(sObjectId) || this->pushTracked.count(sObjectId)))
        {
            this->hoveredTaxiTarget     = sObjectId;
            this->hoveredTaxiTargetTick = GetTickCount64();
            this->RequestRefresh();
        }

        // Taxi planning: update cursor snap and recompute preview on every mouse move.
        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && !this->taxiPlanActive.empty())
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (settings->osmGraph.IsBuilt())
            {
                EuroScopePlugIn::CPosition rawPos = this->ConvertCoordFromPixelToPosition(Pt);
                GeoPoint                   raw{rawPos.m_Latitude, rawPos.m_Longitude};

                const TaxiRoute& sug = this->taxiSuggested.count(this->taxiPlanActive)
                                           ? this->taxiSuggested.at(this->taxiPlanActive)
                                           : TaxiRoute{};
                this->taxiCursorSnap = settings->osmGraph.SnapForPlanning(raw, sug);

                // Collect raw cursor positions and nearby graph nodes while a middle-drag
                // draw gesture is in progress.  The node set is passed as preferredNodes to
                // FindWaypointRoute after release to bias A* toward the drawn path.
                constexpr double DRAW_SAMPLE_M      = 5.0;
                constexpr double DRAW_SNAP_RADIUS_M = 20.0;
                if (this->taxiMidDrawing &&
                    HaversineM(raw, this->taxiLastDrawnPos) >= DRAW_SAMPLE_M)
                {
                    this->taxiDrawPolyline.push_back(raw);
                    const int nid = settings->osmGraph.NearestNodeId(raw, DRAW_SNAP_RADIUS_M);
                    if (nid >= 0)
                        this->taxiDrawnNodeSet.insert(nid);
                    this->taxiLastDrawnPos = raw;
                }

                auto rt = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
                if (rt.IsValid())
                {
                    EuroScopePlugIn::CPosition rpos = rt.GetPosition().GetPosition();
                    GeoPoint                   origin{rpos.m_Latitude, rpos.m_Longitude};
                    // Use heading for start-node selection only when the aircraft is moving;
                    // a parked aircraft faces the terminal so its heading misleads A* into
                    // snapping to a node behind the stand rather than the taxiway exit.
                    const int    gs      = rt.GetPosition().GetReportedGS();
                    const double heading = rt.GetPosition().GetReportedHeadingTrueNorth();

                    if (this->taxiPlanIsPush)
                    {
                        const double pushDir  = std::fmod(this->taxiPushHeading + 180.0, 360.0);
                        const double taxiDirA = std::fmod(pushDir + 90.0, 360.0);
                        const double taxiDirB = std::fmod(pushDir - 90.0 + 360.0, 360.0);
                        const double ws       = this->taxiPushWingspan;

                        // Re-run candidate search and select pivot by projecting cursor onto
                        // the push axis; swap to a farther taxiway when past its midpoint.
                        auto candidates = settings->osmGraph.PushCandidates(
                            this->taxiPushStandOrigin, pushDir, ws, 200.0);

                        if (!candidates.empty())
                        {
                            // Flat-earth projection of cursor onto push axis from stand.
                            const double cosLat   = std::cos(this->taxiPushStandOrigin.lat *
                                                             std::numbers::pi / 180.0);
                            const double scaleLat = TAXI_EARTH_R * std::numbers::pi / 180.0;
                            const double scaleLon = TAXI_EARTH_R * cosLat * std::numbers::pi / 180.0;
                            const double brngRad  = pushDir * std::numbers::pi / 180.0;
                            const double ux       = std::sin(brngRad);
                            const double uy       = std::cos(brngRad);
                            const double dx       = (this->taxiCursorSnap.lon - this->taxiPushStandOrigin.lon) * scaleLon;
                            const double dy       = (this->taxiCursorSnap.lat - this->taxiPushStandOrigin.lat) * scaleLat;
                            const double t        = dx * ux + dy * uy; // cursor distance along push axis

                            // Pick the candidate whose midpoint boundary the cursor has passed.
                            int sel = 0;
                            for (int i = 1; i < (int)candidates.size(); ++i)
                            {
                                const double mid =
                                    (candidates[i - 1].distM + candidates[i].distM) / 2.0;
                                if (t >= mid)
                                    sel = i;
                            }
                            this->taxiPushOrigin = candidates[sel].pos;
                        }

                        // Build zone from selected pivot.
                        constexpr double MIN_BEARING_M = 15.0;
                        const double     cursorDistM   = HaversineM(this->taxiPushOrigin, this->taxiCursorSnap);

                        if (cursorDistM < MIN_BEARING_M)
                        {
                            this->taxiGreenPreview =
                                BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                              taxiDirA, 50.0, taxiDirB, 50.0, ws);
                        }
                        else
                        {
                            const double brng    = BearingDeg(this->taxiPushOrigin, this->taxiCursorSnap);
                            const double oppBrng = std::fmod(brng + 180.0, 360.0);

                            TaxiRoute cursorArm = settings->osmGraph.FindRoute(
                                this->taxiPushOrigin, this->taxiCursorSnap,
                                ws, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(), -1.0, {});
                            TaxiRoute fixedArm =
                                settings->osmGraph.WalkGraph(this->taxiPushOrigin, oppBrng, 25.0, ws);

                            if (!cursorArm.valid || cursorArm.polyline.size() <= 1)
                            {
                                this->taxiGreenPreview =
                                    BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                                  taxiDirA, 50.0, taxiDirB, 50.0, ws);
                            }
                            else
                            {
                                if (fixedArm.valid && fixedArm.polyline.size() > 1)
                                    cursorArm.polyline.insert(cursorArm.polyline.begin(),
                                                              fixedArm.polyline.rbegin(),
                                                              std::prev(fixedArm.polyline.rend()));
                                this->taxiGreenPreview = cursorArm;
                            }
                        }
                    }
                    else if (!this->taxiMidDrawing)
                    {
                        this->RecalculateTaxiPreview();
                    }
                }
                this->RequestRefresh();
            }
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnOverScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnOverScreenObject", "unknown exception");
    }
}

/// @brief Sets the pressed state when the mouse button goes down over the ACK or Start button.
/// Also handles the GND transfer square, which must be triggered on button-down rather than click
/// because EuroScope's radar target selection logic may consume the full press+release sequence.
void RadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (this->debug)
            GetPlugIn()->DisplayUserMessage("FlowX", "GndXfr",
                                            std::format("BtnDown type={} id={} btn={}", ObjectType, sObjectId, Button).c_str(), true, true, false, false, false);

        if (ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            this->napAckPressed = true;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            this->startBtnPressed = true;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_GND_TRANSFER)
        {
            static_cast<CFlowX_Functions*>(this->GetPlugIn())->Func_GndTransfer(std::string(sObjectId));
            this->RequestRefresh();
        }

        // Middle-button down during taxi planning starts a draw gesture.
        // Preferred nodes from any previous gesture are cleared so each draw
        // represents the intent for one specific segment.
        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING &&
            Button == EuroScopePlugIn::BUTTON_MIDDLE &&
            !this->taxiPlanActive.empty() && !this->taxiPlanIsPush)
        {
            this->taxiMidDrawing = true;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear(); // each gesture replaces the previous
            EuroScopePlugIn::CPosition startRawPos = this->ConvertCoordFromPixelToPosition(Pt);
            GeoPoint                   startRaw{startRawPos.m_Latitude, startRawPos.m_Longitude};
            this->taxiLastDrawnPos = startRaw;
            this->taxiDrawPolyline.push_back(startRaw);
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnButtonDownScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnButtonDownScreenObject", "unknown exception");
    }
}

/// @brief Clears the pressed state when the mouse button is released.
void RadarScreen::OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (ObjectType == SCREEN_OBJECT_NAP_ACK)
        {
            this->napAckPressed = false;
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN)
        {
            this->startBtnPressed = false;
            this->RequestRefresh();
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnButtonUpScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnButtonUpScreenObject", "unknown exception");
    }
}

/// @brief Starts the ACK blink animation on click; the window closes after it completes.
/// Also handles Start button menu toggle, menu item selection, and window close buttons.
void RadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        // ── Taxi planning interactions ────────────────────────────────────────────────
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (!settings->osmGraph.IsBuilt())
            {
                return;
            }

            std::string callsign(sObjectId);
            auto        rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
            if (!rt.IsValid())
            {
                return;
            }

            // Determine push vs. taxi.
            // taxiOnlyZones (config) always force taxi regardless of other conditions.
            // Push = inside grStands + NOT inside taxiOutStands.
            // Taxi = everything else (including inbounds with no flight plan).
            {
                bool isPush    = false;
                bool inTaxiOut = false;

                auto myAptIt = settings->FindMyAirport();
                if (myAptIt != settings->GetAirports().end())
                {
                    const auto& ap  = myAptIt->second;
                    const auto  pos = rt.GetPosition().GetPosition();

                    // taxiOutStands check: used for both push/taxi detection and
                    // forwardOnly routing.  Checked first so the result is available
                    // regardless of taxiOnlyZone membership.
                    for (const auto& [_, poly] : ap.taxiOutStands)
                    {
                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                static_cast<int>(poly.lat.size()),
                                const_cast<double*>(poly.lon.data()),
                                const_cast<double*>(poly.lat.data()),
                                pos.m_Longitude, pos.m_Latitude))
                        {
                            inTaxiOut = true;
                            break;
                        }
                    }

                    // taxiOnlyZones override: always taxi from these aprons.
                    bool inTaxiOnlyZone = false;
                    for (const auto& [_, poly] : ap.taxiOnlyZones)
                    {
                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                static_cast<int>(poly.lat.size()),
                                const_cast<double*>(poly.lon.data()),
                                const_cast<double*>(poly.lat.data()),
                                pos.m_Longitude, pos.m_Latitude))
                        {
                            inTaxiOnlyZone = true;
                            break;
                        }
                    }

                    if (!inTaxiOnlyZone && !inTaxiOut)
                    {
                        // Inside a grStands parking spot → push planning.
                        const std::string ourIcao = myAptIt->first;
                        const std::string prefix  = ourIcao + ":";
                        for (const auto& [key, stand] : settings->GetGrStands())
                        {
                            if (key.size() < prefix.size() ||
                                key.substr(0, prefix.size()) != prefix)
                                continue;
                            if (stand.lat.empty())
                                continue;
                            if (CFlowX_LookupsTools::PointInsidePolygon(
                                    static_cast<int>(stand.lat.size()),
                                    const_cast<double*>(stand.lon.data()),
                                    const_cast<double*>(stand.lat.data()),
                                    pos.m_Longitude, pos.m_Latitude))
                            {
                                isPush = true;
                                break;
                            }
                        }
                    }
                }
                this->taxiPlanIsPush      = isPush;
                this->taxiPlanForwardOnly = inTaxiOut;
            }

            this->taxiPlanActive = callsign;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiLastDrawnPos = {};
            this->taxiOriginPx     = Pt;
            this->taxiGreenPreview = {};

            EuroScopePlugIn::CPosition rpos = rt.GetPosition().GetPosition();
            GeoPoint                   origin{rpos.m_Latitude, rpos.m_Longitude};

            if (this->taxiPlanIsPush)
            {
                const double heading      = rt.GetPosition().GetReportedHeadingTrueNorth();
                this->taxiPushHeading     = heading;
                this->taxiPushStandOrigin = origin;
                const double pushDir      = std::fmod(heading + 180.0, 360.0);

                // Look up aircraft wingspan for restriction filtering.
                double wingspan = 0.0;
                {
                    auto fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
                    if (fp.IsValid())
                        wingspan = settings->GetAircraftWingspan(fp.GetFlightPlanData().GetAircraftFPType());
                }
                this->taxiPushWingspan = wingspan;

                // Find viable taxiway pivots in push direction (wingspan-filtered).
                auto candidates = settings->osmGraph.PushCandidates(origin, pushDir, wingspan, 200.0);
                if (!candidates.empty())
                    this->taxiPushOrigin = candidates[0].pos; // nearest allowed taxiway
                else
                {
                    auto [snapped, snapLabel] = settings->osmGraph.SnapNearest(
                        PushZonePoint(origin, pushDir, 30.0), 80.0);
                    this->taxiPushOrigin = snapped.lat != 0.0 ? snapped
                                                              : PushZonePoint(origin, pushDir, 30.0);
                }

                // Initial zone: 50 m each side along the taxiway (±90° from push direction).
                const double taxiDirA  = std::fmod(pushDir + 90.0, 360.0);
                const double taxiDirB  = std::fmod(pushDir - 90.0 + 360.0, 360.0);
                this->taxiGreenPreview = BuildPushZone(settings->osmGraph, this->taxiPushOrigin,
                                                       taxiDirA, 50.0, taxiDirB, 50.0, wingspan);
            }

            if (!this->taxiPlanIsPush)
            {
                // Taxi planning: compute auto-suggested route to destination.
                GeoPoint    dest{0.0, 0.0};
                std::string ourIcao;
                auto        myAptTaxi  = settings->FindMyAirport();
                const bool  hasAirport = myAptTaxi != settings->GetAirports().end();
                if (hasAirport)
                    ourIcao = myAptTaxi->first;

                auto fp        = GetPlugIn()->FlightPlanSelect(callsign.c_str());
                bool isInbound = false;
                if (fp.IsValid())
                {
                    std::string arrAirport = fp.GetFlightPlanData().GetDestination();
                    to_upper(arrAirport);
                    isInbound = (!ourIcao.empty() && arrAirport == ourIcao);
                }

                auto*                 timers    = static_cast<CFlowX_Timers*>(this->GetPlugIn());
                double                goalBrng  = -1.0;
                std::set<std::string> rwySearch = settings->GetActiveDepRunways();
                if (isInbound)
                {
                    auto standIt = timers->GetStandAssignment().find(callsign);
                    if (standIt != timers->GetStandAssignment().end())
                    {
                        const std::string& standName = standIt->second;
                        std::string        standKey  = ourIcao + ":" + standName;
                        const auto&        ap        = myAptTaxi->second;
                        auto               ovIt      = ap.standRoutingTargets.find(standName);
                        if (ovIt != ap.standRoutingTargets.end())
                        {
                            const auto& srt = ovIt->second;
                            if (srt.type == standRoutingTarget::Type::stand)
                            {
                                standKey = ourIcao + ":" + srt.target;
                                dest     = TaxiGraph::StandApproachPoint(standKey, settings->GetGrStands());
                            }
                            else
                            {
                                GeoPoint hp = settings->osmGraph.HoldingPositionByLabel(srt.target);
                                dest        = (hp.lat != 0.0 || hp.lon != 0.0)
                                                  ? hp
                                                  : TaxiGraph::StandApproachPoint(standKey, settings->GetGrStands());
                            }
                        }
                        else
                        {
                            dest = TaxiGraph::StandApproachPoint(standKey, settings->GetGrStands());
                        }
                        // Use stand heading to constrain goal-node snapping direction.
                        auto stIt = settings->GetGrStands().find(standKey);
                        if (stIt != settings->GetGrStands().end() && stIt->second.heading.has_value())
                            goalBrng = std::fmod(stIt->second.heading.value() + 180.0, 360.0);
                    }
                }
                else if (hasAirport)
                {
                    const auto& ap = myAptTaxi->second;

                    // FP runway takes priority; fall back to controller's active config.
                    if (fp.IsValid())
                    {
                        std::string fpRwy = fp.GetFlightPlanData().GetDepartureRwy();
                        if (!fpRwy.empty())
                            rwySearch = {fpRwy};
                    }

                    if (fp.IsValid())
                    {
                        std::string ann = fp.GetControllerAssignedData().GetFlightStripAnnotation(8);
                        if (ann.length() > 7)
                        {
                            std::string hpName = ann.substr(7);
                            if (!hpName.empty() && hpName.back() == '*')
                                hpName.clear();
                            if (!hpName.empty())
                            {
                                for (const auto& rwyDes : rwySearch)
                                {
                                    auto rwyIt = ap.runways.find(rwyDes);
                                    if (rwyIt == ap.runways.end())
                                        continue;
                                    auto hpIt = rwyIt->second.holdingPoints.find(hpName);
                                    if (hpIt != rwyIt->second.holdingPoints.end())
                                    {
                                        dest = {hpIt->second.centerLat, hpIt->second.centerLon};
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (dest.lat == 0.0 && dest.lon == 0.0)
                        dest = settings->osmGraph.BestDepartureHP(rwySearch, ap);
                }

                if (dest.lat == 0.0 && dest.lon == 0.0)
                    dest = origin;

                // Collect blocked nodes from active push routes.
                std::set<int> blocked;
                for (const auto& [_, pushRoute] : this->pushTracked)
                {
                    auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
                    blocked.insert(b.begin(), b.end());
                }

                const int    gs      = rt.GetPosition().GetReportedGS();
                const double heading = rt.GetPosition().GetReportedHeadingTrueNorth();
                const double taxiWs  = settings->GetAircraftWingspan(
                    GetPlugIn()->FlightPlanSelect(callsign.c_str()).GetFlightPlanData().GetAircraftFPType());

                // Vacation exit filtering: pass WTC and arrival runway for inbound aircraft.
                char        vacWtc = 0;
                std::string vacArrRwy;
                if (isInbound && fp.IsValid())
                {
                    vacWtc    = fp.GetFlightPlanData().GetAircraftWtc();
                    vacArrRwy = timers->GetArrivalRunway(callsign);
                }

                this->taxiSuggested[callsign] = settings->osmGraph.FindRoute(
                    origin, dest, taxiWs, rwySearch, settings->GetActiveArrRunways(), heading, blocked,
                    {}, false, {}, settings->GetDebug(), this->taxiPlanForwardOnly, goalBrng, vacWtc, vacArrRwy);
                this->taxiGreenPreview = this->taxiSuggested[callsign];
                if (!this->taxiSuggested[callsign].valid && settings->GetDebug())
                    settings->LogDebugMessage(
                        callsign + " no route: " + this->taxiSuggested[callsign].debugTrace, "TAXI");
            }
            // Push planning: no suggestion; preview computed on first mouse move.

            if (settings->GetDebug())
                settings->LogDebugMessage(
                    std::string(this->taxiPlanIsPush ? "Push" : "Taxi") +
                        " planning started for " + callsign,
                    "TAXI");

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

            if (this->taxiPlanIsPush)
            {
                // Push planning: always use the current preview (no "near origin" shortcut).
                if (this->taxiGreenPreview.valid)
                {
                    this->pushTracked[this->taxiPlanActive] = this->taxiGreenPreview;
                    this->hoveredTaxiTarget                 = this->taxiPlanActive;
                    this->hoveredTaxiTargetTick             = GetTickCount64();

                    // Set ground state to PUSH.
                    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
                    if (fp.IsValid())
                    {
                        std::string scratchBackup(fp.GetControllerAssignedData().GetScratchPadString());
                        fp.GetControllerAssignedData().SetScratchPadString("PUSH");
                        fp.GetControllerAssignedData().SetScratchPadString(scratchBackup.c_str());
                    }
                    if (settings->GetDebug())
                        settings->LogDebugMessage(
                            this->taxiPlanActive + " push route assigned: " + FormatTaxiRoute(this->taxiGreenPreview), "TAXI");
                }
            }
            else
            {
                const int  dx         = Pt.x - this->taxiOriginPx.x;
                const int  dy         = Pt.y - this->taxiOriginPx.y;
                const bool nearOrigin = (dx * dx + dy * dy) <= 80 * 80;

                TaxiRoute finalRoute = nearOrigin
                                           ? this->taxiSuggested[this->taxiPlanActive]
                                           : this->taxiGreenPreview;

                if (finalRoute.valid)
                {
                    this->pushTracked.erase(this->taxiPlanActive);
                    this->taxiAssigned[this->taxiPlanActive]      = finalRoute;
                    this->taxiAssignedTimes[this->taxiPlanActive] = GetTickCount64();
                    this->taxiTracked[this->taxiPlanActive]       = finalRoute;

                    // Record current position so UpdateTaxiSafety can suppress the
                    // deviation warning until a genuine position update has arrived.
                    auto rtNow = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
                    if (rtNow.IsValid() && rtNow.GetPosition().IsValid())
                    {
                        auto p                                      = rtNow.GetPosition().GetPosition();
                        this->taxiAssignedPos[this->taxiPlanActive] = {p.m_Latitude, p.m_Longitude};
                    }

                    // Assign ground state: TAXI for departures, TXIN for inbounds.
                    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
                    if (fp.IsValid())
                    {
                        std::string ourIcao;
                        auto        myAptState = settings->FindMyAirport();
                        if (myAptState != settings->GetAirports().end())
                            ourIcao = myAptState->first;
                        std::string arr = fp.GetFlightPlanData().GetDestination();
                        to_upper(arr);
                        const bool  inbound      = (!ourIcao.empty() && arr == ourIcao);
                        const char* targetState  = inbound ? "TXIN" : "TAXI";
                        std::string currentState = fp.GetGroundState();
                        if (currentState != targetState)
                        {
                            std::string scratch = fp.GetControllerAssignedData().GetScratchPadString();
                            fp.GetControllerAssignedData().SetScratchPadString(targetState);
                            fp.GetControllerAssignedData().SetScratchPadString(scratch.c_str());
                        }
                        if (inbound)
                        {
                            static_cast<CFlowX_Timers*>(this->GetPlugIn())
                                ->ClearGndTransfer(this->taxiPlanActive);
                            this->gndTransferSquares.erase(this->taxiPlanActive);
                            this->gndTransferSquareTimes.erase(this->taxiPlanActive);
                        }
                        if (settings->GetDebug())
                        {
                            const std::string& cs    = this->taxiPlanActive;
                            auto               sugIt = this->taxiSuggested.find(cs);
                            if (sugIt != this->taxiSuggested.end())
                            {
                                settings->LogDebugMessage(
                                    cs + " suggested: " + FormatTaxiRoute(sugIt->second), "TAXI");
                                if (!sugIt->second.debugTrace.empty())
                                    settings->LogDebugMessage(cs + " sug trace:\n" + sugIt->second.debugTrace, "TAXI");
                            }
                            settings->LogDebugMessage(
                                cs + " assigned:  " + FormatTaxiRoute(finalRoute), "TAXI");
                            if (!finalRoute.debugTrace.empty())
                                settings->LogDebugMessage(cs + " asg trace:\n" + finalRoute.debugTrace, "TAXI");
                        }

                        // Log test case template when "Log TAXI tests" is enabled.
                        if (this->logTaxiTests && finalRoute.valid && !finalRoute.polyline.empty())
                        {
                            const std::string& cs = this->taxiPlanActive;

                            // Build runway config string
                            std::string depStr, arrStr;
                            for (const auto& r : settings->GetActiveDepRunways())
                            {
                                if (!depStr.empty())
                                    depStr += '/';
                                depStr += r;
                            }
                            for (const auto& r : settings->GetActiveArrRunways())
                            {
                                if (!arrStr.empty())
                                    arrStr += '/';
                                arrStr += r;
                            }
                            std::string rwyCfg = depStr + "_" + arrStr;

                            // Resolve from/to as type-prefixed labels (HP:/STAND:/GEO:).
                            std::string fromLabel, toLabel;
                            if (inbound)
                            {
                                fromLabel = settings->osmGraph.PrefixedLabel(finalRoute.polyline.front(), 50.0);
                                // Use stand assignment for inbound destination.
                                auto* timers  = static_cast<CFlowX_Timers*>(this->GetPlugIn());
                                auto  standIt = timers->GetStandAssignment().find(cs);
                                toLabel       = (standIt != timers->GetStandAssignment().end() && !standIt->second.empty())
                                                    ? "STAND:" + standIt->second
                                                    : settings->osmGraph.PrefixedLabel(finalRoute.polyline.back(), 50.0);
                            }
                            else
                            {
                                // Outbound: detect stand from raw aircraft position via
                                // grStands point-in-polygon.  Exclude taxiOnlyZones (e.g.
                                // GAC) — these are large areas, not individual stands.
                                auto        posIt   = this->taxiAssignedPos.find(cs);
                                GeoPoint    rawFrom = (posIt != this->taxiAssignedPos.end())
                                                          ? posIt->second
                                                          : finalRoute.polyline.front();
                                std::string standName;
                                auto        myAptLog = settings->FindMyAirport();
                                if (myAptLog != settings->GetAirports().end())
                                {
                                    const std::string ourIcao = myAptLog->first;
                                    const auto&       ap      = myAptLog->second;
                                    const std::string prefix  = ourIcao + ":";
                                    for (const auto& [key, stand] : settings->GetGrStands())
                                    {
                                        if (key.size() < prefix.size() || key.substr(0, prefix.size()) != prefix)
                                            continue;
                                        if (stand.lat.empty())
                                            continue;
                                        if (CFlowX_LookupsTools::PointInsidePolygon(
                                                static_cast<int>(stand.lat.size()),
                                                const_cast<double*>(stand.lon.data()),
                                                const_cast<double*>(stand.lat.data()),
                                                rawFrom.lon, rawFrom.lat))
                                        {
                                            std::string sn = key.substr(prefix.size());
                                            if (!ap.taxiOnlyZones.contains(sn))
                                                standName = sn;
                                            break;
                                        }
                                    }
                                }
                                fromLabel = standName.empty()
                                                ? std::format("GEO:{:.6f},{:.6f}", rawFrom.lat, rawFrom.lon)
                                                : "STAND:" + standName;
                                toLabel   = settings->osmGraph.PrefixedLabel(finalRoute.polyline.back(), 50.0);
                            }

                            // Get wingspan
                            double wingspan = settings->GetAircraftWingspan(
                                fp.GetFlightPlanData().GetAircraftFPType());

                            // Build waypoints JSON array (intermediate planning points)
                            std::string waypointsJson = "[";
                            for (size_t i = 0; i < this->taxiWaypoints.size(); ++i)
                            {
                                if (i > 0)
                                    waypointsJson += ",";
                                waypointsJson += std::format(R"({{"lat":{:.6f},"lon":{:.6f}}})",
                                                             this->taxiWaypoints[i].lat,
                                                             this->taxiWaypoints[i].lon);
                            }
                            waypointsJson += "]";

                            // Build wayRefs JSON array (reference; not part of test schema)
                            std::string wayRefsJson = "[";
                            for (size_t i = 0; i < finalRoute.wayRefs.size(); ++i)
                            {
                                if (i > 0)
                                    wayRefsJson += ",";
                                std::string escaped = finalRoute.wayRefs[i];
                                for (size_t p = 0; (p = escaped.find('"', p)) != std::string::npos; p += 2)
                                    escaped.insert(p, 1, '\\');
                                wayRefsJson += "\"" + escaped + "\"";
                            }
                            wayRefsJson += "]";

                            std::string typeStr   = inbound ? "inbound" : "outbound";
                            std::string direction = inbound ? "IN" : "OUT";

                            // Swingover origin/bearing — only emitted when swingover was active.
                            // The test replays the tail segment from swingoverOrigin with the
                            // captured bearing so A* locks onto the correct lane.
                            std::string swingoverJson = "false";
                            if (this->taxiSwingoverActive && this->taxiSwingoverFixedSeg.valid)
                            {
                                swingoverJson = std::format(
                                    R"({{"origin":{{"lat":{:.6f},"lon":{:.6f}}},"bearing":{:.2f}}})",
                                    this->taxiSwingoverOrigin.lat,
                                    this->taxiSwingoverOrigin.lon,
                                    this->taxiSwingoverBearing);
                            }

                            // Heading for forward-only candidate selection in test replay.
                            const double hdg = (rtNow.IsValid() && rtNow.GetPosition().IsValid())
                                                   ? rtNow.GetPosition().GetReportedHeadingTrueNorth()
                                                   : -1.0;

                            const double distMin = std::max(0.0, finalRoute.totalDistM - 50.0);
                            const double distMax = finalRoute.totalDistM + 50.0;

                            std::string testJson = std::format(
                                R"({{"name":"{}_{}/{} to {}","type":"{}","runwayConfig":"{}",)"
                                R"("from":"{}","to":"{}","wingspan":{:.1f},"heading":{:.1f},)"
                                R"("waypoints":{},"swingover":{},)"
                                R"("distanceRange":[{:.0f},{:.0f}],)"
                                R"("mustInclude":[],"mustNotInclude":[],"wayRefs":{}}})",
                                rwyCfg, cs, direction, toLabel, typeStr, rwyCfg,
                                fromLabel, toLabel, wingspan, hdg,
                                waypointsJson, swingoverJson,
                                distMin, distMax, wayRefsJson);

                            this->GetPlugIn()->DisplayUserMessage(
                                PLUGIN_NAME, "TAXI", testJson.c_str(), true, true, true, false, false);
                            settings->LogToFile(testJson, "TAXI");
                        }
                    }
                }
            }

            this->taxiPlanActive.clear();
            this->taxiPlanIsPush        = false;
            this->taxiPlanForwardOnly   = false;
            this->taxiSwingoverActive   = false;
            this->taxiSwingoverFixedSeg = {};
            this->taxiSwingoverOrigin   = {};
            this->taxiSwingoverBearing  = -1.0;
            this->taxiAltPrevDown       = false;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiGreenPreview = {};
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_MIDDLE)
        {
            const bool wasDraw   = this->taxiMidDrawing;
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            // taxiDrawnNodeSet stays active — it was populated during OnOverScreenObject
            // sampling and will bias the next FindWaypointRoute call toward the drawn path.
            this->taxiWaypoints.push_back(this->taxiCursorSnap);
            this->RecalculateTaxiPreview();
            if (!this->taxiGreenPreview.valid)
            {
                // Route failed — undo the waypoint so the user can retry.
                this->taxiWaypoints.pop_back();
                if (wasDraw)
                {
                    this->taxiDrawnNodeSet.clear();
                }
                this->RecalculateTaxiPreview();
                auto* snd = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                if (snd->GetSoundNoRoute())
                {
                    std::string wav = (std::filesystem::path(GetPluginDirectory()) / "noRoute.wav").string();
                    PlaySoundA(wav.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                }
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TAXI_PLANNING && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            this->taxiSuggested.erase(this->taxiPlanActive);
            this->taxiPlanActive.clear();
            this->taxiPlanIsPush        = false;
            this->taxiPlanForwardOnly   = false;
            this->taxiSwingoverActive   = false;
            this->taxiSwingoverFixedSeg = {};
            this->taxiSwingoverOrigin   = {};
            this->taxiSwingoverBearing  = -1.0;
            this->taxiAltPrevDown       = false;
            this->taxiWaypoints.clear();
            this->taxiMidDrawing = false;
            this->taxiDrawPolyline.clear();
            this->taxiDrawnNodeSet.clear();
            this->taxiGreenPreview = {};
            this->RequestRefresh();
            return;
        }
        // ─────────────────────────────────────────────────────────────────────────────

        if (ObjectType == SCREEN_OBJECT_WIN_CLOSE && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto*       settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (id == "approachEst")
            {
                settings->ToggleApproachEstVisible();
            }
            else if (id == "depRate")
            {
                settings->ToggleDepRateVisible();
            }
            else if (id == "twrOut")
            {
                settings->ToggleTwrOutboundVisible();
            }
            else if (id == "twrIn")
            {
                settings->ToggleTwrInboundVisible();
            }
            else if (id == "weather")
            {
                settings->ToggleWeatherVisible();
            }
            else if (id == "diflis")
            {
                settings->SetDiflisVisible(false);
                settings->SetDiflisPoppedOut(false);
                this->diflisPopout.reset();
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_WIN_POPOUT && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto*       settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
            if (id == "approachEst" && !this->approachEstPopout)
            {
                settings->SetApproachEstPoppedOut(true);
                this->CreateApproachEstPopout(settings);
            }
            else if (id == "depRate" && !this->depRatePopout)
            {
                settings->SetDepRatePoppedOut(true);
                this->CreateDepRatePopout(settings);
            }
            else if (id == "weather" && !this->weatherPopout)
            {
                settings->SetWeatherPoppedOut(true);
                this->CreateWeatherPopout(settings);
            }
            else if (id == "diflis" && !this->diflisPopout)
            {
                settings->SetDiflisPoppedOut(true);
                this->CreateDiflisPopout(settings);
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_DIFLIS_MAXIMIZE_BTN && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            if (this->diflisPopout)
            {
                auto* s    = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                bool  next = !this->diflisPopout->IsMaximized();
                this->diflisPopout->SetMaximized(next);
                s->SetDiflisPopoutMaximized(next);
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_DIFLIS_TOPMOST_BTN && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            if (this->diflisPopout)
            {
                auto* s    = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                bool  next = !this->diflisPopout->IsTopmost();
                this->diflisPopout->SetTopmost(next);
                s->SetDiflisPopoutTopmost(next);
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_DIFLIS_UNDO_BTN && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            this->DiflisUndo();
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_START_BTN && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            this->startMenuOpen = !this->startMenuOpen;
            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_START_MENU_ITEM && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            int         idx = -1;
            if (sep != std::string::npos)
            {
                idx            = std::stoi(id.substr(sep + 1));
                auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                if (idx == 0) // Redo CLR flags
                {
                    static_cast<CFlowX_Functions*>(this->GetPlugIn())->RedoFlags();
                }
                else if (idx == 1) // Save positions
                {
                    static_cast<CFlowX_Timers*>(this->GetPlugIn())->SaveWindowPositions();
                }
                else if (idx == 12) // Dismiss QNH
                {
                    static_cast<CFlowX_Functions*>(this->GetPlugIn())->DismissQnh();
                }
                else if (idx == 2) // Debug mode
                {
                    settings->ToggleDebug();
                    this->debug = static_cast<CFlowX_Base*>(this->GetPlugIn())->GetDebug();
                }
                else if (idx == 3) // Autostore FPLN
                {
                    settings->ToggleAutoRestore();
                }
                else if (idx == 13)
                {
                    settings->ToggleUpdateCheck();
                }
                else if (idx == 14)
                {
                    settings->ToggleFlashOnMessage();
                }
                else if (idx == 15)
                {
                    settings->ToggleAutoParked();
                }
                else if (idx == 16)
                {
                    settings->ToggleApproachEstVisible();
                }
                else if (idx == 17)
                {
                    settings->ToggleApprEstColors();
                }
                else if (idx == 18)
                {
                    settings->ToggleAutoScratchpadClear();
                }
                else if (idx == 19)
                {
                    settings->ToggleSoundAirborne();
                }
                else if (idx == 20)
                {
                    settings->ToggleSoundGndTransfer();
                }
                else if (idx == 21)
                {
                    settings->ToggleSoundReadyTakeoff();
                }
                else if (idx == 27)
                {
                    settings->ToggleSoundTaxiConflict();
                }
                else if (idx == 30)
                {
                    settings->ToggleSoundNoRoute();
                }
                else if (idx == 28)
                {
                    settings->ToggleHpAutoScratch();
                }
                else if (idx == 22) // Update TAXI info
                {
                    settings->StartOsmFetch();
                }
                else if (idx == 23) // Show TAXI network
                {
                    this->showTaxiOverlay = !this->showTaxiOverlay;
                }
                else if (idx == 24) // Show TAXI labels
                {
                    this->showTaxiLabels = !this->showTaxiLabels;
                }
                else if (idx == 25) // Show TAXI routes
                {
                    this->showTaxiRoutes = !this->showTaxiRoutes;
                }
                else if (idx == 29) // Show TAXI graph
                {
                    this->showTaxiGraph = !this->showTaxiGraph;
                }
                else if (idx == 31) // Log TAXI tests
                {
                    this->logTaxiTests = !this->logTaxiTests;
                }
                else if (idx == 32) // DIFLIS window
                {
                    settings->ToggleDiflisVisible();
                }
                else if (idx == 26) // Clear all TAXI routes
                {
                    this->pushTracked.clear();
                    this->taxiTracked.clear();
                    this->taxiAssigned.clear();
                    this->taxiAssignedTimes.clear();
                    this->taxiAssignedPos.clear();
                    this->taxiSuggested.clear();
                    this->taxiDeviations.clear();
                    this->taxiConflicts.clear();
                }
                else if (idx == 4)
                {
                    settings->ToggleDepRateVisible();
                }
                else if (idx == 5)
                {
                    settings->ToggleTwrOutboundVisible();
                }
                else if (idx == 6)
                {
                    settings->ToggleTwrInboundVisible();
                }
                else if (idx == 7)
                {
                    settings->ToggleWeatherVisible();
                }
                else if (idx == 8)
                {
                    settings->DecreaseFontOffset();
                }
                else if (idx == 9)
                {
                    settings->IncreaseFontOffset();
                }
                else if (idx == 10)
                {
                    settings->DecreaseBgOpacity();
                }
                else if (idx == 11)
                {
                    settings->IncreaseBgOpacity();
                }
            }
            // Keep menu open for window toggles (4-7, 16), notification toggles (19-21), and TAXI toggles (23-24); close for all others
            if (idx < 4 || idx == 12 || idx == 22 || (idx >= 13 && idx != 16 && idx != 17 && idx < 19))
            {
                this->startMenuOpen = false;
            }

            std::string clickSnd = GetPluginDirectory() + "\\click.wav";
            PlaySoundA(clickSnd.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_NAP_ACK && this->napAckClickTick == 0)
        {
            this->napAckClickTick = GetTickCount64();
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_WEATHER_ROW)
        {
            static_cast<CFlowX_Timers*>(this->GetPlugIn())->AckWeather(std::string(sObjectId));
            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_DEP_TAG_SID_DOT)
        {
            std::string id(sObjectId);
            auto        sep = id.rfind('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);

            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            if (Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                auto ctrl = this->GetPlugIn()->ControllerMyself();
                if (!ctrl.IsController() || ctrl.GetFacility() != 3)
                {
                    return;
                }

                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_NEXT_FREQ, "",
                                       PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, Pt, Area);
            }
            else if (Button == EuroScopePlugIn::BUTTON_RIGHT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_SORT, "",
                                       PLUGIN_NAME, TAG_FUNC_APPEND_QUEUE_POS, Pt, Area);
            }

            this->RequestRefresh();
            return;
        }

        if (ObjectType == SCREEN_OBJECT_TWR_OUT_CELL)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);
            std::string col      = id.substr(sep + 1);

            // Select the aircraft as ASEL (mirrors what clicking any tag item does in the default lists)
            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            // Locate the cached row so we can pass the current item string to StartTagFunction
            const TwrOutboundRowCache* rowPtr = nullptr;
            for (const auto& r : this->twrOutboundRowsCache)
            {
                if (r.callsign == callsign)
                {
                    rowPtr = &r;
                    break;
                }
            }
            if (!rowPtr)
            {
                return;
            }
            const TwrOutboundRowCache& r = *rowPtr;

            if (col == "STS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_TAKE_OFF : TAG_FUNC_LINE_UP;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, r.status.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "FREQ")
            {
                if (Button == EuroScopePlugIn::BUTTON_LEFT)
                {
                    this->StartTagFunction(callsign.c_str(),
                                           PLUGIN_NAME, TAG_ITEM_TWR_NEXT_FREQ, r.nextFreq.tag.c_str(),
                                           PLUGIN_NAME, TAG_FUNC_TRANSFER_NEXT, Pt, Area);
                }
            }
            else if (col == "RWY" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_ASSIGNED_RUNWAY, r.rwy.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY, Pt, Area);
            }
            else if (col == "SID" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_SAMESID, r.sameSid.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_SID, Pt, Area);
            }
            else if (col == "HP")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_REQUEST_HP : TAG_FUNC_ASSIGN_HP;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_HP, r.hp.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "QPOS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_APPEND_QUEUE_POS : TAG_FUNC_ASSIGN_QUEUE_POS;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TWR_SORT, r.queuePos.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }

            this->RequestRefresh();
        }

        if (ObjectType == SCREEN_OBJECT_TWR_IN_CELL)
        {
            std::string id(sObjectId);
            auto        sep = id.find('|');
            if (sep == std::string::npos)
            {
                return;
            }
            std::string callsign = id.substr(0, sep);
            std::string col      = id.substr(sep + 1);

            // Select the aircraft as ASEL
            auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (aselFp.IsValid())
            {
                GetPlugIn()->SetASELAircraft(aselFp);
            }

            // Locate the cached row for item strings
            const TwrInboundRowCache* rowPtr = nullptr;
            for (const auto& r : this->twrInboundRowsCache)
            {
                if (r.callsign == callsign)
                {
                    rowPtr = &r;
                    break;
                }
            }
            if (!rowPtr)
            {
                return;
            }
            const TwrInboundRowCache& r = *rowPtr;

            if ((col == "TTT" || col == "RWY") && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TTT, r.ttt.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_TAKE_HANDOFF, Pt, Area);
            }
            else if (col == "CS")
            {
                int funcId = (Button == EuroScopePlugIn::BUTTON_RIGHT) ? TAG_FUNC_MISSED_APP : TAG_FUNC_CLRD_TO_LAND;
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_TTT, r.ttt.tag.c_str(),
                                       PLUGIN_NAME, funcId, Pt, Area);
            }
            else if (col == "GATE")
            {
                if (Button == EuroScopePlugIn::BUTTON_LEFT)
                {
                    this->StartTagFunction(callsign.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.tag.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_FUNC_STAND_MENU, Pt, Area);
                }
                else
                {
                    this->StartTagFunction(callsign.c_str(),
                                           GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.tag.c_str(),
                                           PLUGIN_NAME, TAG_FUNC_STAND_AUTO, Pt, Area);
                }
            }
            else if (col == "ARW" && Button == EuroScopePlugIn::BUTTON_LEFT)
            {
                this->StartTagFunction(callsign.c_str(),
                                       PLUGIN_NAME, TAG_ITEM_ASSIGNED_ARR_RUNWAY, r.arrRwy.tag.c_str(),
                                       nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_ASSIGNED_RUNWAY, Pt, Area);
            }

            this->RequestRefresh();
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnClickScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnClickScreenObject", "unknown exception");
    }
}

/// @brief Double-click handler: on the STS column, reverts LINEUP ground state back to TAXI.
void RadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        // Double right-click on a ground aircraft target → clear its assigned taxi route.
        if (ObjectType == SCREEN_OBJECT_TAXI_TARGET && Button == EuroScopePlugIn::BUTTON_RIGHT)
        {
            const std::string cs(sObjectId);
            this->pushTracked.erase(cs);
            this->taxiTracked.erase(cs);
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
            this->taxiAssignedPos.erase(cs);
            this->taxiSuggested.erase(cs);
            if (this->taxiPlanActive == cs)
            {
                this->taxiPlanActive.clear();
                this->taxiPlanIsPush        = false;
                this->taxiPlanForwardOnly   = false;
                this->taxiSwingoverActive   = false;
                this->taxiSwingoverFixedSeg = {};
                this->taxiSwingoverOrigin   = {};
                this->taxiSwingoverBearing  = -1.0;
                this->taxiAltPrevDown       = false;
                this->taxiWaypoints.clear();
                this->taxiGreenPreview = {};
            }
            this->RequestRefresh();
            return;
        }

        if (ObjectType != SCREEN_OBJECT_TWR_OUT_CELL || Button != EuroScopePlugIn::BUTTON_LEFT)
        {
            return;
        }

        std::string id(sObjectId);
        auto        sep = id.find('|');
        if (sep == std::string::npos)
        {
            return;
        }
        std::string callsign = id.substr(0, sep);
        std::string col      = id.substr(sep + 1);

        if (col != "STS")
        {
            return;
        }

        auto fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (!fp.IsValid())
        {
            return;
        }

        // Only revert when the aircraft is currently in LINEUP state
        auto statusIt = std::ranges::find_if(this->twrOutboundRowsCache,
                                             [&callsign](const TwrOutboundRowCache& r)
                                             { return r.callsign == callsign; });
        if (statusIt == this->twrOutboundRowsCache.end())
        {
            return;
        }
        if (statusIt->status.tag != "LINE UP")
        {
            return;
        }

        GetPlugIn()->SetASELAircraft(fp);
        this->StartTagFunction(callsign.c_str(),
                               PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, statusIt->status.tag.c_str(),
                               PLUGIN_NAME, TAG_FUNC_REVERT_TO_TAXI, Pt, Area);
        this->RequestRefresh();
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnDoubleClickScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnDoubleClickScreenObject", "unknown exception");
    }
}
/// @brief Accumulates drag offsets for departure overlays and resets the drag origin on mouse release.
/// @param ObjectType EuroScope type identifier of the dragged object.
/// @param sObjectId Callsign string identifying which departure overlay is being dragged.
/// @param Pt Current cursor position.
/// @param Area Bounding rectangle of the object.
/// @param Released True when the mouse button has been released.
void RadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released)
{
    try
    {
        auto dragWindow = [&](POINT& windowPos, POINT& lastDrag)
        {
            if (lastDrag.x == -1 || lastDrag.y == -1)
            {
                lastDrag = Pt;
            }
            windowPos.x += Pt.x - lastDrag.x;
            windowPos.y += Pt.y - lastDrag.y;
            lastDrag = Pt;
            if (Released)
            {
                lastDrag = {-1, -1};
            }
        };

        std::string objId(sObjectId);
        if (objId == "APPROACH_EST")
        {
            dragWindow(this->approachEstWindowPos, this->approachEstLastDrag);
            return;
        }
        if (objId == "APPROACH_EST_RESIZE")
        {
            if (this->approachEstPopout)
            {
                // Direct-drag path: onDirectDrag_ already resized the HWND on the popout thread.
                // approachEstWindowW/H are synced from GetContentW/H() each frame — applying another
                // delta here would double-count. Just persist the final size on release.
                if (Released)
                {
                    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                    settings->SetApproachEstPopoutSize(this->approachEstWindowW, this->approachEstWindowH);
                    this->approachEstResizeLastDrag = {-1, -1};
                }
                return;
            }
            if (this->approachEstResizeLastDrag.x == -1 || this->approachEstResizeLastDrag.y == -1)
            {
                this->approachEstResizeLastDrag = Pt;
            }
            this->approachEstWindowW        = std::max(120, this->approachEstWindowW + (int)(Pt.x - this->approachEstResizeLastDrag.x));
            this->approachEstWindowH        = std::max(200, this->approachEstWindowH + (int)(Pt.y - this->approachEstResizeLastDrag.y));
            this->approachEstResizeLastDrag = Pt;
            if (Released)
            {
                this->approachEstResizeLastDrag = {-1, -1};
            }
            return;
        }
        if (objId == "DEPRATE")
        {
            dragWindow(this->depRateWindowPos, this->depRateLastDrag);
            return;
        }
        if (objId == "TWROUT")
        {
            dragWindow(this->twrOutboundWindowPos, this->twrOutboundLastDrag);
            return;
        }
        if (objId == "TWRIN")
        {
            dragWindow(this->twrInboundWindowPos, this->twrInboundLastDrag);
            return;
        }
        if (objId == "NAPWIN")
        {
            dragWindow(this->napWindowPos, this->napLastDrag);
            return;
        }
        if (objId == "WXATIS")
        {
            dragWindow(this->weatherWindowPos, this->weatherLastDrag);
            return;
        }
        if (objId == "DIFLIS_RESIZE")
        {
            // Popout-only: onDirectDrag_ has already resized the HWND; persist on release.
            if (Released && this->diflisPopout)
            {
                auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
                settings->SetDiflisPopoutSize(this->diflisWindowW, this->diflisWindowH);
            }
            return;
        }

        if (ObjectType == SCREEN_OBJECT_DIFLIS_STRIP)
        {
            std::string callsign(sObjectId);
            if (this->diflisDragCallsign.empty())
            {
                this->diflisDragCallsign = callsign;
                this->diflisDragFromGroup.clear();
                for (const auto& cs : this->diflisStripsCache)
                {
                    if (cs.callsign == callsign)
                    {
                        this->diflisDragFromGroup = cs.resolvedGroupId;
                        break;
                    }
                }
            }
            this->diflisDragCursor = Pt;
            if (Released)
            {
                std::string toGroup;
                for (const auto& gr : this->diflisGroupRects)
                {
                    if (PtInRect(&gr.second, Pt))
                    {
                        toGroup = gr.first;
                        break;
                    }
                }
                if (!toGroup.empty())
                    this->DiflisMoveStrip(this->diflisDragCallsign, this->diflisDragFromGroup, toGroup);
                this->diflisDragCallsign.clear();
                this->diflisDragFromGroup.clear();
                this->diflisDragCursor = {-9999, -9999};
            }
            this->RequestRefresh();
            return;
        }

        auto depInfo = this->radarTargetDepartureInfos.find(std::string(sObjectId));
        if (depInfo != this->radarTargetDepartureInfos.end())
        {
            if (depInfo->second.lastDrag.x == -1 || depInfo->second.lastDrag.y == -1)
            {
                depInfo->second.lastDrag = Pt;
            }

            depInfo->second.dragX += Pt.x - depInfo->second.lastDrag.x;
            depInfo->second.dragY += Pt.y - depInfo->second.lastDrag.y;

            depInfo->second.lastDrag = Pt;

            if (Released)
            {
                depInfo->second.lastDrag.x = -1;
                depInfo->second.lastDrag.y = -1;
            }
        }
    }
    catch (const std::exception& e)
    {
        WriteExceptionToLog("OnMoveScreenObject", e.what());
    }
    catch (...)
    {
        WriteExceptionToLog("OnMoveScreenObject", "unknown exception");
    }
}
