/**
 * @file RadarScreen_Taxi.cpp
 * @brief RadarScreen partial implementation: taxi overlay, taxi/push planning, route preview, conflicts, dead-ends, and push-zone helpers.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "RadarScreen.h"
#include "CFlowX_Base.h"
#include "CFlowX_WindowCache.h"
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

/// @brief Recomputes taxiGreenPreview from the current taxiCursorSnap and planning state.
/// Called from OnOverScreenObject on every mouse move and from UpdateSwingoverState on ALT toggle
/// so the new route is visible immediately without waiting for the next mouse event.
void RadarScreen::RecalculateTaxiPreview()
{
    if (this->taxiPlanActive.empty() || this->taxiPlanIsPush || this->taxiMidDrawing)
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    auto  rt       = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
    if (!rt.IsValid())
        return;

    const EuroScopePlugIn::CPosition rpos    = rt.GetPosition().GetPosition();
    const GeoPoint                   origin  = {rpos.m_Latitude, rpos.m_Longitude};
    const double                     heading = rt.GetPosition().GetReportedHeadingTrueNorth();

    std::set<int> blocked;
    for (const auto& [_, pushRoute] : this->pushTracked)
    {
        auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
        blocked.insert(b.begin(), b.end());
    }

    const double taxiWs = settings->GetAircraftWingspan(
        GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str()).GetFlightPlanData().GetAircraftFPType());

    if (this->taxiSwingoverActive && this->taxiSwingoverFixedSeg.valid)
    {
        TaxiRoute tail = settings->osmGraph.FindWaypointRoute(
            this->taxiSwingoverOrigin, this->taxiWaypoints, this->taxiCursorSnap,
            taxiWs, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(),
            this->taxiSwingoverBearing, blocked);
        if (tail.valid)
        {
            TaxiRoute combined = this->taxiSwingoverFixedSeg;
            combined.totalDistM += tail.totalDistM;
            combined.totalCost += tail.totalCost;
            combined.exitBearing = tail.exitBearing;
            for (const auto& pt : tail.polyline)
                combined.polyline.push_back(pt);
            for (const auto& ref : tail.wayRefs)
                combined.wayRefs.push_back(ref);
            this->taxiGreenPreview = combined;
        }
        else
        {
            this->taxiGreenPreview = this->taxiSwingoverFixedSeg;
        }
    }
    else
    {
        const bool hasDrawnNodes = !this->taxiDrawnNodeSet.empty() && !this->taxiWaypoints.empty();
        // If the cursor is still on top of the last waypoint (e.g. right after
        // middle-button release), route to the waypoint only — avoids a degenerate
        // zero-length final segment that loops back on itself.
        const bool cursorAtLastWp =
            !this->taxiWaypoints.empty() &&
            HaversineM(this->taxiCursorSnap, this->taxiWaypoints.back()) < 5.0;
        const GeoPoint        dest = cursorAtLastWp ? this->taxiWaypoints.back() : this->taxiCursorSnap;
        std::vector<GeoPoint> wps =
            cursorAtLastWp
                ? std::vector<GeoPoint>(this->taxiWaypoints.begin(), this->taxiWaypoints.end() - 1)
                : this->taxiWaypoints;
        this->taxiGreenPreview = settings->osmGraph.FindWaypointRoute(
            origin, wps, dest,
            taxiWs, settings->GetActiveDepRunways(), settings->GetActiveArrRunways(),
            heading, blocked, hasDrawnNodes, this->taxiDrawnNodeSet, settings->GetDebug(),
            this->taxiPlanForwardOnly);
    }
}

/// @brief Detects ALT keypress each BEFORE_TAGS frame and toggles taxiSwingoverActive when it fires.
void RadarScreen::UpdateSwingoverState()
{
    if (this->taxiPlanActive.empty() || this->taxiPlanIsPush)
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

    const bool vkMenu  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool vkLMenu = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    const bool vkRMenu = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    const bool altNow  = vkMenu || vkLMenu || vkRMenu;
    if (altNow && !this->taxiAltPrevDown)
    {
        this->taxiSwingoverActive = !this->taxiSwingoverActive;

        if (this->taxiSwingoverActive)
        {
            this->taxiSwingoverFixedSeg = {};
            auto myAptIt                = settings->FindMyAirport();
            auto rt                     = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
            if (myAptIt != settings->GetAirports().end() && rt.IsValid())
            {
                const auto&  ap     = myAptIt->second;
                const auto   rpos   = rt.GetPosition().GetPosition();
                const double hdg    = rt.GetPosition().GetReportedHeadingTrueNorth();
                const double taxiWs = settings->GetAircraftWingspan(
                    GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str()).GetFlightPlanData().GetAircraftFPType());
                const GeoPoint acPos{rpos.m_Latitude, rpos.m_Longitude};

                const std::string curRef = settings->osmGraph.WayRefAt(acPos, 40.0);
                if (!curRef.empty())
                {
                    auto sr = settings->osmGraph.SwingoverSnap(
                        acPos, curRef, ap.taxiLaneSwingoverPairs, taxiWs, hdg);
                    if (sr.valid)
                    {
                        TaxiRoute fixedSeg;
                        fixedSeg.polyline.push_back(acPos);
                        fixedSeg.polyline.push_back(sr.crossPt);
                        for (const auto& pt : sr.sbendPts)
                            fixedSeg.polyline.push_back(pt);
                        fixedSeg.polyline.push_back(sr.partnerPt);
                        fixedSeg.wayRefs            = {curRef};
                        fixedSeg.valid              = true;
                        fixedSeg.totalDistM         = HaversineM(acPos, sr.partnerPt);
                        this->taxiSwingoverFixedSeg = fixedSeg;
                        this->taxiSwingoverOrigin   = sr.partnerPt;
                        this->taxiSwingoverBearing  = sr.brngAtPartner;
                    }
                    else
                        this->taxiSwingoverActive = false;
                }
                else
                    this->taxiSwingoverActive = false;
            }
            else
                this->taxiSwingoverActive = false;
        }
        else
        {
            this->taxiSwingoverFixedSeg = {};
        }

        if (settings->GetDebug())
            settings->LogDebugMessage(
                std::format("Swingover {} (VK_MENU={} VK_LMENU={} VK_RMENU={})",
                            this->taxiSwingoverActive ? "ON" : "OFF",
                            vkMenu, vkLMenu, vkRMenu),
                "TAXI");

        // Recompute and redraw immediately so the controller sees the new route
        // without having to nudge the mouse first.
        this->RecalculateTaxiPreview();
        this->RequestRefresh();
    }
    this->taxiAltPrevDown = altNow;
}

/// @brief Draws the OSM taxiway/taxilane polylines, derived runway centrelines, holding-position circles, and way labels.
void RadarScreen::DrawTaxiOverlay(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

    if (!(this->showTaxiOverlay || this->showTaxiLabels))
        return;
    if (settings->osmData.ways.empty() && settings->osmData.holdingPositions.empty())
        return;

    // Pass 1 — draw polylines, one pen per way.
    if (this->showTaxiOverlay)
        for (const auto& way : settings->osmData.ways)
        {
            const COLORREF col  = (way.type == AerowayType::Taxiway_HoldingPoint)   ? RGB(255, 80, 80)
                                  : (way.type == AerowayType::Taxiway_Intersection) ? RGB(80, 200, 80)
                                  : (way.type == AerowayType::Taxilane)             ? RGB(0, 200, 255)
                                  : (way.type == AerowayType::Runway)               ? RGB(255, 140, 0)
                                                                                    : RGB(255, 220, 0);
            HPEN           pen  = CreatePen(PS_SOLID, 2, col);
            HPEN           prev = static_cast<HPEN>(SelectObject(hDC, pen));

            bool first = true;
            for (const auto& gp : way.geometry)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                const POINT pt  = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }

            SelectObject(hDC, prev);
            DeleteObject(pen);
        }

    // Pass 1b — derived runway centrelines (from TaxiGraph, not OSM ways).
    if (this->showTaxiOverlay)
    {
        HPEN rwPen  = CreatePen(PS_SOLID, 3, RGB(255, 140, 0));
        HPEN rwPrev = static_cast<HPEN>(SelectObject(hDC, rwPen));
        for (const auto& cl : settings->osmGraph.runwayCentrelines)
        {
            bool first = true;
            for (const auto& gp : cl)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                const POINT pt  = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }
        }
        SelectObject(hDC, rwPrev);
        DeleteObject(rwPen);
    }

    HFONT labelFont = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = static_cast<HFONT>(SelectObject(hDC, labelFont));
    SetBkMode(hDC, TRANSPARENT);

    auto drawLabel = [&](POINT pt, const std::string& text, COLORREF col)
    {
        RECT m = {0, 0, 200, 30};
        DrawTextA(hDC, text.c_str(), -1, &m, DT_CALCRECT | DT_SINGLELINE);
        const int hw = (m.right - m.left) / 2 + 2;
        const int hh = (m.bottom - m.top) / 2 + 1;
        RECT      bg = {pt.x - hw, pt.y - hh, pt.x + hw, pt.y + hh};
        auto      br = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hDC, &bg, br);
        DeleteObject(br);
        SetTextColor(hDC, col);
        DrawTextA(hDC, text.c_str(), -1, &bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    // Pass 3 — holding position nodes: filled orange circles with designator label.
    if (this->showTaxiOverlay && !settings->osmData.holdingPositions.empty())
    {
        HPEN   hpPen       = CreatePen(PS_SOLID, 1, RGB(255, 140, 0));
        HBRUSH hpBrush     = CreateSolidBrush(RGB(255, 140, 0));
        HPEN   prevHpPen   = static_cast<HPEN>(SelectObject(hDC, hpPen));
        HBRUSH prevHpBrush = static_cast<HBRUSH>(SelectObject(hDC, hpBrush));

        for (const auto& hp : settings->osmData.holdingPositions)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = hp.pos.lat;
            pos.m_Longitude = hp.pos.lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            Ellipse(hDC, pt.x - 7, pt.y - 7, pt.x + 7, pt.y + 7);

            const std::string& lbl = hp.ref.empty() ? hp.name : hp.ref;
            if (!lbl.empty() && this->showTaxiLabels)
                drawLabel({pt.x + 13, pt.y}, lbl, RGB(160, 80, 0));
        }

        SelectObject(hDC, prevHpBrush);
        SelectObject(hDC, prevHpPen);
        DeleteObject(hpBrush);
        DeleteObject(hpPen);
    }

    // Pass 2 — way labels; gated on showTaxiLabels.
    if (this->showTaxiLabels)
    {
        std::set<std::string> drawnIsxLabels;
        std::set<std::string> drawnHpLabels;

        for (const auto& way : settings->osmData.ways)
        {
            if (way.geometry.size() < 2)
                continue;

            const std::string& lbl = way.ref.empty() ? way.name : way.ref;
            if (lbl.empty())
                continue;

            if (way.type == AerowayType::Taxiway_Intersection)
            {
                if (drawnIsxLabels.contains(lbl))
                    continue;
                drawnIsxLabels.insert(lbl);
            }

            if (way.type == AerowayType::Taxiway_HoldingPoint)
            {
                if (drawnHpLabels.contains(lbl))
                    continue;
                drawnHpLabels.insert(lbl);
            }

            const COLORREF textCol = (way.type == AerowayType::Taxiway_HoldingPoint)   ? RGB(200, 60, 60)
                                     : (way.type == AerowayType::Taxiway_Intersection) ? RGB(60, 160, 60)
                                     : (way.type == AerowayType::Taxilane)             ? RGB(0, 160, 210)
                                     : (way.type == AerowayType::Runway)               ? RGB(200, 90, 0)
                                                                                       : RGB(180, 140, 0);

            double accum   = 0.0;
            double nextLbl = 250.0;
            bool   placed  = false;

            for (size_t k = 1; k < way.geometry.size(); ++k)
            {
                const auto&  a    = way.geometry[k - 1];
                const auto&  b    = way.geometry[k];
                const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
                const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
                const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
                const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
                const double h    = std::sin(dLat / 2) * std::sin(dLat / 2) +
                                    cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
                const double seg  = 6'371'000.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));

                if (accum + seg >= nextLbl)
                {
                    EuroScopePlugIn::CPosition pos;
                    pos.m_Latitude  = b.lat;
                    pos.m_Longitude = b.lon;
                    drawLabel(ConvertCoordFromPositionToPixel(pos), lbl, textCol);
                    nextLbl += 500.0;
                    placed = true;
                }
                accum += seg;
            }

            if (!placed)
            {
                const size_t               mid = way.geometry.size() / 2;
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = way.geometry[mid].lat;
                pos.m_Longitude = way.geometry[mid].lon;
                drawLabel(ConvertCoordFromPositionToPixel(pos), lbl, textCol);
            }
        }
    }

    // Pass 4 & 5 — config polygons for the active airport only.
    auto overlayAptIt = settings->FindMyAirport();
    if (this->showTaxiOverlay && overlayAptIt != settings->GetAirports().end())
    {
        const auto& apt = overlayAptIt->second;

        // Pass 4 — holding point polygons in purple.
        HPEN   hpPen     = CreatePen(PS_SOLID, 2, RGB(180, 80, 220));
        HPEN   prevHpPen = static_cast<HPEN>(SelectObject(hDC, hpPen));
        HBRUSH nullBrush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH prevBrush = static_cast<HBRUSH>(SelectObject(hDC, nullBrush));
        for (const auto& [rwyName, rwy] : apt.runways)
        {
            for (const auto& [hpName, hp] : rwy.holdingPoints)
            {
                if (hp.lat.size() < 3)
                    continue;
                std::vector<POINT> pts(hp.lat.size());
                for (size_t k = 0; k < hp.lat.size(); ++k)
                {
                    EuroScopePlugIn::CPosition pos;
                    pos.m_Latitude  = hp.lat[k];
                    pos.m_Longitude = hp.lon[k];
                    pts[k]          = ConvertCoordFromPositionToPixel(pos);
                }
                Polygon(hDC, pts.data(), static_cast<int>(pts.size()));
            }
        }
        SelectObject(hDC, prevBrush);
        SelectObject(hDC, prevHpPen);
        DeleteObject(hpPen);

        // Pass 5 — taxi-out stand polygons in turquoise.
        HPEN   toPen     = CreatePen(PS_SOLID, 2, RGB(0, 200, 180));
        HPEN   prevToPen = static_cast<HPEN>(SelectObject(hDC, toPen));
        HBRUSH nullBr    = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH prevBr    = static_cast<HBRUSH>(SelectObject(hDC, nullBr));
        for (const auto& [name, zone] : apt.taxiOutStands)
        {
            if (zone.lat.size() < 3)
                continue;
            std::vector<POINT> pts(zone.lat.size());
            for (size_t k = 0; k < zone.lat.size(); ++k)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = zone.lat[k];
                pos.m_Longitude = zone.lon[k];
                pts[k]          = ConvertCoordFromPositionToPixel(pos);
            }
            Polygon(hDC, pts.data(), static_cast<int>(pts.size()));
        }
        SelectObject(hDC, prevBr);
        SelectObject(hDC, prevToPen);
        DeleteObject(toPen);
    }

    SelectObject(hDC, prevFont);
    DeleteObject(labelFont);
}

/// @brief Draws a single TaxiRoute polyline with the given GDI pen colour and width.
void RadarScreen::DrawRoutePolyline(HDC hDC, const TaxiRoute& route, COLORREF col, int width)
{
    if (!route.valid || route.polyline.empty())
        return;
    HPEN pen   = CreatePen(PS_SOLID, width, col);
    HPEN prev  = static_cast<HPEN>(SelectObject(hDC, pen));
    bool first = true;
    for (const auto& gp : route.polyline)
    {
        EuroScopePlugIn::CPosition pos;
        pos.m_Latitude  = gp.lat;
        pos.m_Longitude = gp.lon;
        POINT pt        = ConvertCoordFromPositionToPixel(pos);
        if (first)
        {
            MoveToEx(hDC, pt.x, pt.y, nullptr);
            first = false;
        }
        else
            LineTo(hDC, pt.x, pt.y);
    }
    SelectObject(hDC, prev);
    DeleteObject(pen);
}

/// @brief Draws confirmed (2-second flash) and persistently tracked taxi routes.
void RadarScreen::DrawTaxiRoutes(HDC hDC)
{
    // Draw confirmed taxi routes (2-second flash); prune entries older than 2 s.
    if (!this->taxiAssigned.empty())
    {
        const ULONGLONG          now = GetTickCount64();
        std::vector<std::string> toErase;
        for (const auto& [cs, _] : this->taxiAssigned)
        {
            auto tIt = this->taxiAssignedTimes.find(cs);
            if (tIt == this->taxiAssignedTimes.end() || now - tIt->second >= 2000)
                toErase.push_back(cs);
        }
        for (const auto& cs : toErase)
        {
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
        }

        HPEN pen  = CreatePen(PS_SOLID, 3, RGB(80, 220, 80));
        HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));
        for (const auto& [cs, route] : this->taxiAssigned)
        {
            if (!route.valid || route.polyline.empty())
                continue;
            bool first = true;
            for (const auto& gp : route.polyline)
            {
                EuroScopePlugIn::CPosition pos;
                pos.m_Latitude  = gp.lat;
                pos.m_Longitude = gp.lon;
                POINT pt        = ConvertCoordFromPositionToPixel(pos);
                if (first)
                {
                    MoveToEx(hDC, pt.x, pt.y, nullptr);
                    first = false;
                }
                else
                    LineTo(hDC, pt.x, pt.y);
            }
        }
        SelectObject(hDC, prev);
        DeleteObject(pen);
    }

    // Draw persistent tracked routes (remaining portion from current aircraft position).
    // Helper: draws one tracked route clipped to the remaining leg; returns true if done.
    auto drawTrackedRoute = [&](HDC dc, const std::string& cs, const TaxiRoute& route,
                                std::vector<std::string>& done) -> void
    {
        if (!route.valid || route.polyline.size() < 2)
            return;
        auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
            return;

        const EuroScopePlugIn::CPosition rawPos = rt.GetPosition().GetPosition();
        const GeoPoint                   acGeo{rawPos.m_Latitude, rawPos.m_Longitude};
        const POINT                      acPt        = ConvertCoordFromPositionToPixel(rawPos);
        size_t                           closestIdx  = 0;
        long                             closestDist = LONG_MAX;
        for (size_t i = 0; i < route.polyline.size(); ++i)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = route.polyline[i].lat;
            pos.m_Longitude = route.polyline[i].lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            const long  dx  = pt.x - acPt.x;
            const long  dy  = pt.y - acPt.y;
            const long  dSq = dx * dx + dy * dy;
            if (dSq < closestDist)
            {
                closestDist = dSq;
                closestIdx  = i;
            }
        }

        // (c) Airborne fallback: aircraft is above field elevation + margin.
        // Clears stale routes after takeoff (fast-forwarded replay, missed TAKE OFF press, etc.).
        auto*         settings           = static_cast<CFlowX_Settings*>(this->GetPlugIn());
        auto          elevAptIt          = settings->FindMyAirport();
        const int     fieldElevFt        = (elevAptIt != settings->GetAirports().end())
                                               ? elevAptIt->second.fieldElevation
                                               : 0;
        constexpr int AIRBORNE_MARGIN_FT = 50;
        if (rt.GetPosition().GetPressureAltitude() > fieldElevFt + AIRBORNE_MARGIN_FT)
        {
            done.push_back(cs);
            return;
        }

        if (closestIdx == route.polyline.size() - 1)
        {
            const GeoPoint& finalPt = route.polyline[closestIdx];

            // (a) Aircraft is geographically close to the final node.
            constexpr double ARRIVAL_THRESH_M = 80.0;
            const bool       nearEnd          = HaversineM(acGeo, finalPt) <= ARRIVAL_THRESH_M;

            // (b) Aircraft has passed the final node (skipped over due to infrequent
            //     position updates): dot(A→B, B→P) > 0.
            bool pastEnd = false;
            if (route.polyline.size() >= 2)
            {
                const GeoPoint& prevPt = route.polyline[closestIdx - 1];
                const double    abLat  = finalPt.lat - prevPt.lat;
                const double    abLon  = finalPt.lon - prevPt.lon;
                const double    bpLat  = acGeo.lat - finalPt.lat;
                const double    bpLon  = acGeo.lon - finalPt.lon;
                pastEnd                = (abLat * bpLat + abLon * bpLon) > 0.0;
            }

            if (nearEnd || pastEnd)
            {
                done.push_back(cs);
                return;
            }
        }

        // Connector from aircraft to nearest route point: yellow to indicate
        // the aircraft is off-network or has deviated from the assigned route.
        EuroScopePlugIn::CPosition snapPos;
        snapPos.m_Latitude  = route.polyline[closestIdx].lat;
        snapPos.m_Longitude = route.polyline[closestIdx].lon;
        const POINT snapPt  = ConvertCoordFromPositionToPixel(snapPos);
        if (acPt.x != snapPt.x || acPt.y != snapPt.y)
        {
            HPEN yellowPen = CreatePen(PS_SOLID, 2, RGB(255, 220, 0));
            HPEN savedPen  = static_cast<HPEN>(SelectObject(dc, yellowPen));
            MoveToEx(dc, acPt.x, acPt.y, nullptr);
            LineTo(dc, snapPt.x, snapPt.y);
            SelectObject(dc, savedPen);
            DeleteObject(yellowPen);
        }

        // Remaining route in the caller's pen colour (green).
        MoveToEx(dc, snapPt.x, snapPt.y, nullptr);
        for (size_t i = closestIdx + 1; i < route.polyline.size(); ++i)
        {
            EuroScopePlugIn::CPosition pos;
            pos.m_Latitude  = route.polyline[i].lat;
            pos.m_Longitude = route.polyline[i].lon;
            const POINT pt  = ConvertCoordFromPositionToPixel(pos);
            LineTo(dc, pt.x, pt.y);
        }
    };

    // Expire the hover state after 500 ms so it clears when the mouse moves away.
    constexpr ULONGLONG HOVER_MS = 1000;
    if (!this->hoveredTaxiTarget.empty() &&
        GetTickCount64() - this->hoveredTaxiTargetTick >= HOVER_MS)
        this->hoveredTaxiTarget.clear();

    // Draw hovered aircraft route individually (visible even when showTaxiRoutes is off).
    std::vector<std::string> routesDone;
    if (!this->hoveredTaxiTarget.empty())
    {
        auto it = this->taxiTracked.find(this->hoveredTaxiTarget);
        if (it != this->taxiTracked.end())
        {
            HPEN pen  = CreatePen(PS_SOLID, 3, RGB(140, 255, 140));
            HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));
            drawTrackedRoute(hDC, it->first, it->second, routesDone);
            SelectObject(hDC, prev);
            DeleteObject(pen);
        }
        else if (!this->pushTracked.count(this->hoveredTaxiTarget))
            this->hoveredTaxiTarget.clear(); // push-only aircraft: keep for DrawPlanningRoutes
    }

    if (!this->showTaxiRoutes || this->taxiTracked.empty())
    {
        for (const auto& cs : routesDone)
        {
            this->taxiTracked.erase(cs);
            this->taxiAssigned.erase(cs);
            this->taxiAssignedTimes.erase(cs);
            this->taxiAssignedPos.erase(cs);
        }
        return;
    }

    HPEN pen  = CreatePen(PS_SOLID, 2, RGB(80, 220, 80));
    HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));

    for (const auto& [cs, route] : this->taxiTracked)
    {
        if (cs == this->hoveredTaxiTarget)
            continue; // already drawn above with highlight pen
        drawTrackedRoute(hDC, cs, route, routesDone);
    }

    SelectObject(hDC, prev);
    DeleteObject(pen);

    for (const auto& cs : routesDone)
    {
        this->taxiTracked.erase(cs);
        this->taxiAssigned.erase(cs);
        this->taxiAssignedTimes.erase(cs);
        this->taxiAssignedPos.erase(cs);
    }
}

/// @brief Draws active push routes, the yellow suggested route, and the magenta/light-blue planning preview.
void RadarScreen::DrawPlanningRoutes(HDC hDC)
{
    // Draw push routes: only when "Show routes" is on or the pushing aircraft is hovered.
    // Red during taxi-planning mode (active obstacle), orange otherwise.
    if (!this->pushTracked.empty())
    {
        const bool     inTaxiPlan = !this->taxiPlanActive.empty() && !this->taxiPlanIsPush;
        const COLORREF pushCol    = inTaxiPlan ? RGB(220, 50, 50) : RGB(255, 140, 0);
        const COLORREF pushHovCol = inTaxiPlan ? RGB(255, 80, 80) : RGB(255, 180, 60);
        const bool     hoverFresh =
            !this->hoveredTaxiTarget.empty() &&
            GetTickCount64() - this->hoveredTaxiTargetTick < 1000ULL;
        const bool hoverIsPush =
            hoverFresh && this->pushTracked.count(this->hoveredTaxiTarget);

        if (hoverIsPush)
            this->DrawRoutePolyline(hDC, this->pushTracked.at(this->hoveredTaxiTarget),
                                    pushHovCol, 3);

        if (this->showTaxiRoutes)
            for (const auto& [cs, pushRoute] : this->pushTracked)
            {
                if (cs == this->hoveredTaxiTarget)
                    continue; // already drawn above with highlight colour
                this->DrawRoutePolyline(hDC, pushRoute, pushCol, 3);
            }
    }

    if (this->taxiPlanActive.empty())
        return;

    if (!this->taxiPlanIsPush)
    {
        auto sugIt = this->taxiSuggested.find(this->taxiPlanActive);
        if (sugIt != this->taxiSuggested.end())
            this->DrawRoutePolyline(hDC, sugIt->second, RGB(255, 220, 0), 2); // yellow suggestion
    }

    const COLORREF previewCol = this->taxiPlanIsPush ? RGB(100, 200, 255) : RGB(255, 0, 220);
    this->DrawRoutePolyline(hDC, this->taxiGreenPreview, previewCol, 3); // light-blue (push) or magenta (taxi)

    // Draw the middle-drag intent path as a thin orange polyline while the gesture is active.
    if (this->taxiMidDrawing && this->taxiDrawPolyline.size() >= 2)
    {
        HPEN drawPen = CreatePen(PS_SOLID, 3, RGB(0, 240, 255));
        HPEN oldPen  = static_cast<HPEN>(SelectObject(hDC, drawPen));
        for (size_t pi = 1; pi < this->taxiDrawPolyline.size(); ++pi)
        {
            EuroScopePlugIn::CPosition pa, pb;
            pa.m_Latitude  = this->taxiDrawPolyline[pi - 1].lat;
            pa.m_Longitude = this->taxiDrawPolyline[pi - 1].lon;
            pb.m_Latitude  = this->taxiDrawPolyline[pi].lat;
            pb.m_Longitude = this->taxiDrawPolyline[pi].lon;
            POINT ptA      = ConvertCoordFromPositionToPixel(pa);
            POINT ptB      = ConvertCoordFromPositionToPixel(pb);
            MoveToEx(hDC, ptA.x, ptA.y, nullptr);
            LineTo(hDC, ptB.x, ptB.y);
        }
        SelectObject(hDC, oldPen);
        DeleteObject(drawPen);
    }

    // Draw via-point markers as small cyan squares.
    for (const auto& wp : this->taxiWaypoints)
    {
        EuroScopePlugIn::CPosition wpos;
        wpos.m_Latitude  = wp.lat;
        wpos.m_Longitude = wp.lon;
        POINT  pt        = ConvertCoordFromPositionToPixel(wpos);
        HPEN   wpen      = CreatePen(PS_SOLID, 1, RGB(0, 220, 220));
        HBRUSH wbrush    = CreateSolidBrush(RGB(0, 220, 220));
        SelectObject(hDC, wpen);
        SelectObject(hDC, wbrush);
        Rectangle(hDC, pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4);
        DeleteObject(wbrush);
        DeleteObject(wpen);
    }

    this->DrawPushDeadEnds(hDC);
}

/// @brief Draws a green filled square near each landed inbound awaiting GND handoff; registers each as a clickable screen object.
void RadarScreen::DrawGndTransferSquares(HDC hDC)
{
    if (this->gndTransferSquares.empty())
    {
        return;
    }

    SetBkMode(hDC, TRANSPARENT);
    ULONGLONG now = GetTickCount64();

    for (const auto& callSign : this->gndTransferSquares)
    {
        EuroScopePlugIn::CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callSign.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
        {
            continue;
        }

        ULONGLONG elapsedSec = this->gndTransferSquareTimes.contains(callSign)
                                   ? (now - this->gndTransferSquareTimes.at(callSign)) / 1000
                                   : 0;
        COLORREF  color      = elapsedSec >= 35   ? TAG_COLOR_RED
                               : elapsedSec >= 25 ? TAG_COLOR_YELLOW
                                                  : TAG_COLOR_GREEN;

        auto brush = CreateSolidBrush(color);
        auto pen   = CreatePen(PS_SOLID, 1, color);
        SelectObject(hDC, brush);
        SelectObject(hDC, pen);

        POINT pt = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
        RECT  sq = {pt.x - 22, pt.y + 10, pt.x - 10, pt.y + 22};
        Rectangle(hDC, sq.left, sq.top, sq.right, sq.bottom);
        AddScreenObject(SCREEN_OBJECT_GND_TRANSFER, callSign.c_str(), sq, true, "");

        DeleteObject(pen);
        DeleteObject(brush);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Taxi safety monitoring
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Returns a GeoPoint offset from @p origin by @p distM metres along @p bearingDeg.
GeoPoint PushZonePoint(const GeoPoint& origin, double bearingDeg, double distM)
{
    constexpr double PI   = 3.14159265358979323846;
    constexpr double R    = 6371000.0;
    const double     d    = distM / R;
    const double     lat1 = origin.lat * PI / 180.0;
    const double     lon1 = origin.lon * PI / 180.0;
    const double     brng = bearingDeg * PI / 180.0;
    const double     lat2 = std::asin(std::sin(lat1) * std::cos(d) +
                                      std::cos(lat1) * std::sin(d) * std::cos(brng));
    const double     lon2 =
        lon1 + std::atan2(std::sin(brng) * std::sin(d) * std::cos(lat1),
                          std::cos(d) - std::sin(lat1) * std::sin(lat2));
    return {lat2 * 180.0 / PI, lon2 * 180.0 / PI};
}

/// @brief Builds a push-zone polyline entirely on the taxiway network.
///
/// Walks @p armADistM in @p armABrng and @p armBDistM in @p armBBrng from @p pivot,
/// then concatenates: armB-end → pivot → armA-end.
TaxiRoute BuildPushZone(const TaxiGraph& graph, const GeoPoint& pivot,
                               double armABrng, double armADistM,
                               double armBBrng, double armBDistM,
                               double wingspanM = 0.0)
{
    TaxiRoute armA = graph.WalkGraph(pivot, armABrng, armADistM, wingspanM);
    TaxiRoute armB = graph.WalkGraph(pivot, armBBrng, armBDistM, wingspanM);

    TaxiRoute combined;
    combined.valid = armA.valid || armB.valid;

    // Lay out: reverse(armB) then armA, skipping the duplicate pivot point.
    if (armB.valid && !armB.polyline.empty())
        combined.polyline.insert(combined.polyline.end(), armB.polyline.rbegin(), armB.polyline.rend());

    if (armA.valid && !armA.polyline.empty())
    {
        auto start = armA.polyline.cbegin();
        if (!combined.polyline.empty())
            ++start; // first point of armA == pivot, already present
        combined.polyline.insert(combined.polyline.end(), start, armA.polyline.cend());
    }

    return combined;
}

void RadarScreen::UpdateTaxiSafety()
{
    // Throttle: recompute at most once every 250 ms.
    const ULONGLONG now = GetTickCount64();
    if (now - this->taxiSafetyLastTickMs < 250)
        return;
    this->taxiSafetyLastTickMs = now;

    this->taxiDeviations.clear();
    this->taxiConflicts.clear();

    // Auto-clear taxi route when aircraft enters any holding point on its departure runway.
    {
        auto* settings   = static_cast<CFlowX_Settings*>(this->GetPlugIn());
        auto  clearAptIt = settings->FindMyAirport();
        if (clearAptIt != settings->GetAirports().end())
        {
            const auto&              ap = clearAptIt->second;
            std::vector<std::string> toErase;
            for (const auto& [cs, route] : this->taxiTracked)
            {
                auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
                if (!rt.IsValid() || !rt.GetPosition().IsValid())
                    continue;
                auto fp = GetPlugIn()->FlightPlanSelect(cs.c_str());
                if (!fp.IsValid())
                    continue;
                std::string depRwy = fp.GetFlightPlanData().GetDepartureRwy();
                auto        rwyIt  = ap.runways.find(depRwy);
                if (rwyIt == ap.runways.end())
                    continue;
                const auto pos = rt.GetPosition().GetPosition();
                for (const auto& [hpName, hp] : rwyIt->second.holdingPoints)
                {
                    if (hp.lat.empty())
                        continue;
                    if (CFlowX_LookupsTools::PointInsidePolygon(
                            static_cast<int>(hp.lat.size()),
                            const_cast<double*>(hp.lon.data()),
                            const_cast<double*>(hp.lat.data()),
                            pos.m_Longitude, pos.m_Latitude))
                    {
                        toErase.push_back(cs);
                        break;
                    }
                }
            }
            for (const auto& cs : toErase)
            {
                this->taxiTracked.erase(cs);
                this->taxiAssigned.erase(cs);
                this->taxiAssignedTimes.erase(cs);
                this->taxiAssignedPos.erase(cs);
            }
        }
    }

    if (this->taxiTracked.empty())
        return;

    static const TaxiNetworkConfig kDefaultNC{};
    auto*                          safetySettings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    auto                           safetyAptIt    = safetySettings->FindMyAirport();
    const TaxiNetworkConfig&       nc             = (safetyAptIt != safetySettings->GetAirports().end())
                                                        ? safetyAptIt->second.taxiNetworkConfig
                                                        : kDefaultNC;

    const double     DEVIATION_THRESH_M = nc.safety.deviationThreshM;
    const double     MIN_GS_KT          = nc.safety.minSpeedKt;
    constexpr double KT_TO_MS           = 0.514444;
    const double     MAX_PREDICT_S      = nc.safety.maxPredictS;
    const double     CONFLICT_DELTA_S   = nc.safety.conflictDeltaS;
    const double     SAME_DIR_DEG       = nc.safety.sameDirDeg;

    struct TimedPt
    {
        GeoPoint pos;
        double   t; // seconds from now
    };
    std::map<std::string, std::vector<TimedPt>> timedPaths;

    for (const auto& [cs, route] : this->taxiTracked)
    {
        if (!route.valid || route.polyline.size() < 2)
            continue;
        auto rt = GetPlugIn()->RadarTargetSelect(cs.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid())
            continue;

        const double   gs_kt = rt.GetPosition().GetReportedGS();
        const GeoPoint acPos{rt.GetPosition().GetPosition().m_Latitude,
                             rt.GetPosition().GetPosition().m_Longitude};

        // ── Deviation check ──────────────────────────────────────────────────
        // Suppress until the aircraft's position has changed from where it was when
        // the route was assigned — ensures the first position update after confirmation
        // is evaluated rather than the same stale position that was used during planning.
        constexpr double ASSIGN_POS_THRESH_M = 5.0;
        auto             assignPosIt         = this->taxiAssignedPos.find(cs);
        bool             suppressDeviation   = false;
        if (assignPosIt != this->taxiAssignedPos.end())
        {
            if (HaversineM(acPos, assignPosIt->second) >= ASSIGN_POS_THRESH_M)
                this->taxiAssignedPos.erase(assignPosIt); // position has moved — start checking
            else
                suppressDeviation = true;
        }
        if (!suppressDeviation)
        {
            double minDist = std::numeric_limits<double>::max();
            for (size_t i = 1; i < route.polyline.size(); ++i)
                minDist = std::min(minDist,
                                   PointToSegmentDistM(acPos, route.polyline[i - 1], route.polyline[i]));
            if (gs_kt > MIN_GS_KT && minDist > DEVIATION_THRESH_M)
                this->taxiDeviations.insert(cs);
        }

        // ── Build timed predicted path ───────────────────────────────────────
        if (gs_kt <= MIN_GS_KT)
            continue;
        const double gs_ms = gs_kt * KT_TO_MS;

        // Find the closest segment by perpendicular distance.
        size_t bestSeg  = 0;
        double bestDist = std::numeric_limits<double>::max();
        for (size_t i = 1; i < route.polyline.size(); ++i)
        {
            double d = PointToSegmentDistM(acPos, route.polyline[i - 1], route.polyline[i]);
            if (d < bestDist)
            {
                bestDist = d;
                bestSeg  = i; // index of the B-end node of the closest segment
            }
        }

        std::vector<TimedPt> path;
        path.push_back({acPos, 0.0});
        double t = 0.0;

        // Distance from aircraft position to the next node (B-end of closest segment).
        t += HaversineM(acPos, route.polyline[bestSeg]) / gs_ms;
        if (t <= MAX_PREDICT_S)
            path.push_back({route.polyline[bestSeg], t});

        for (size_t i = bestSeg + 1; i < route.polyline.size(); ++i)
        {
            t += HaversineM(route.polyline[i - 1], route.polyline[i]) / gs_ms;
            if (t > MAX_PREDICT_S)
                break;
            path.push_back({route.polyline[i], t});
        }

        if (path.size() >= 2)
            timedPaths[cs] = std::move(path);
    }

    // ── Pairwise conflict check ───────────────────────────────────────────────
    std::vector<std::string> keys;
    keys.reserve(timedPaths.size());
    for (const auto& [cs, _] : timedPaths)
        keys.push_back(cs);

    // Pre-compute per-path axis-aligned bounding boxes for the path-level AABB guard.
    struct PathBBox
    {
        double minLat, maxLat, minLon, maxLon;
    };
    std::map<std::string, PathBBox> pathBBoxes;
    for (const auto& [cs, path] : timedPaths)
    {
        PathBBox bb{path[0].pos.lat, path[0].pos.lat, path[0].pos.lon, path[0].pos.lon};
        for (const auto& tp : path)
        {
            bb.minLat = std::min(bb.minLat, tp.pos.lat);
            bb.maxLat = std::max(bb.maxLat, tp.pos.lat);
            bb.minLon = std::min(bb.minLon, tp.pos.lon);
            bb.maxLon = std::max(bb.maxLon, tp.pos.lon);
        }
        pathBBoxes[cs] = bb;
    }

    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto&    pathA = timedPaths[keys[i]];
        const PathBBox bbA   = pathBBoxes[keys[i]];
        for (size_t j = i + 1; j < keys.size(); ++j)
        {
            const auto&    pathB = timedPaths[keys[j]];
            const PathBBox bbB   = pathBBoxes[keys[j]];

            // Path-level AABB: skip all segment work when predicted paths don't overlap spatially.
            if (bbA.maxLat < bbB.minLat || bbA.minLat > bbB.maxLat ||
                bbA.maxLon < bbB.minLon || bbA.minLon > bbB.maxLon)
                continue;

            for (size_t a = 1; a < pathA.size(); ++a)
            {
                for (size_t b = 1; b < pathB.size(); ++b)
                {
                    // Segment-level AABB: skip SegmentIntersectGeo for non-overlapping segments.
                    if (std::max(pathA[a - 1].pos.lat, pathA[a].pos.lat) <
                            std::min(pathB[b - 1].pos.lat, pathB[b].pos.lat) ||
                        std::min(pathA[a - 1].pos.lat, pathA[a].pos.lat) >
                            std::max(pathB[b - 1].pos.lat, pathB[b].pos.lat) ||
                        std::max(pathA[a - 1].pos.lon, pathA[a].pos.lon) <
                            std::min(pathB[b - 1].pos.lon, pathB[b].pos.lon) ||
                        std::min(pathA[a - 1].pos.lon, pathA[a].pos.lon) >
                            std::max(pathB[b - 1].pos.lon, pathB[b].pos.lon))
                        continue;

                    GeoPoint isxPt;
                    if (!SegmentIntersectGeo(pathA[a - 1].pos, pathA[a].pos,
                                             pathB[b - 1].pos, pathB[b].pos, isxPt))
                        continue;

                    // Same-direction exclusion: skip if the intersecting segments
                    // OR their successor segments travel in the same direction.
                    // The successor check catches cases where a short connector
                    // segment (aircraft position → first route node) has an
                    // unreliable bearing dominated by lateral offset, as well as
                    // same-route following through turns where the crossing
                    // segments straddle a turn vertex with divergent local bearings.
                    const double bearA = BearingDeg(pathA[a - 1].pos, pathA[a].pos);
                    const double bearB = BearingDeg(pathB[b - 1].pos, pathB[b].pos);
                    if (BearingDiff(bearA, bearB) < SAME_DIR_DEG)
                        continue;
                    if (a + 1 < pathA.size() && b + 1 < pathB.size())
                    {
                        const double nextBearA = BearingDeg(pathA[a].pos, pathA[a + 1].pos);
                        const double nextBearB = BearingDeg(pathB[b].pos, pathB[b + 1].pos);
                        if (BearingDiff(nextBearA, nextBearB) < SAME_DIR_DEG)
                            continue;
                    }

                    // Interpolate arrival time at the intersection.
                    const double distA = HaversineM(pathA[a - 1].pos, pathA[a].pos);
                    const double fracA = (distA > 0.1)
                                             ? std::clamp(HaversineM(pathA[a - 1].pos, isxPt) / distA, 0.0, 1.0)
                                             : 0.0;
                    const double tA    = pathA[a - 1].t + fracA * (pathA[a].t - pathA[a - 1].t);

                    const double distB = HaversineM(pathB[b - 1].pos, pathB[b].pos);
                    const double fracB = (distB > 0.1)
                                             ? std::clamp(HaversineM(pathB[b - 1].pos, isxPt) / distB, 0.0, 1.0)
                                             : 0.0;
                    const double tB    = pathB[b - 1].t + fracB * (pathB[b].t - pathB[b - 1].t);

                    if (std::abs(tA - tB) < CONFLICT_DELTA_S)
                        this->taxiConflicts.push_back({keys[i], keys[j], isxPt, tA, tB, a, b});
                }
            }
        }
    }

    // ── Sound trigger: play once per conflict pair after it persists in <15 s window for 2 s ──
    std::set<std::string> activeKeys;
    std::set<std::string> under15Keys;
    for (const auto& c : this->taxiConflicts)
    {
        const std::string key = std::min(c.csA, c.csB) + "|" + std::max(c.csA, c.csB);
        activeKeys.insert(key);
        if (std::min(c.tA, c.tB) < 15.0)
            under15Keys.insert(key);
    }

    // Track first-seen timestamp for each under-15s conflict; remove entries that left the window.
    for (const auto& key : under15Keys)
    {
        if (!this->taxiConflictFirstSeen.contains(key))
            this->taxiConflictFirstSeen[key] = now;
    }
    std::erase_if(this->taxiConflictFirstSeen,
                  [&](const auto& kv)
                  { return !under15Keys.contains(kv.first); });

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    for (const auto& key : under15Keys)
    {
        if (!this->taxiConflictSoundPlayed.contains(key) &&
            now - this->taxiConflictFirstSeen[key] >= 2000)
        {
            if (settings->GetSoundTaxiConflict())
            {
                const std::filesystem::path wav =
                    std::filesystem::path(GetPluginDirectory()) / "taxiConflict.wav";
                PlaySoundA(wav.string().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            }
            this->taxiConflictSoundPlayed.insert(key);
            break; // at most one sound per cycle
        }
    }
    std::erase_if(this->taxiConflictSoundPlayed,
                  [&](const std::string& k)
                  { return !activeKeys.contains(k); });
}

void RadarScreen::DrawTaxiGraph(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->osmGraph.IsBuilt())
        return;

    const auto& nodes = settings->osmGraph.Nodes();
    const auto& adj   = settings->osmGraph.Adj();

    auto toPixel = [&](const GeoPoint& gp) -> POINT
    {
        EuroScopePlugIn::CPosition pos;
        pos.m_Latitude  = gp.lat;
        pos.m_Longitude = gp.lon;
        return ConvertCoordFromPositionToPixel(pos);
    };

    // Build flow-controlled wayRef set from active generic + config flow rules.
    const auto activeFlowRules = settings->osmGraph.GetActiveFlowRules(
        settings->GetActiveDepRunways(), settings->GetActiveArrRunways());
    std::set<std::string> flowRefs;
    for (const auto& rule : activeFlowRules)
        flowRefs.insert(rule.taxiway);

    // Pass 1 — edges, coloured by type and cost multiplier (cost / distance in metres).
    //   green  — flow-controlled taxiway (wayRef has an active flow rule)
    //   white  — neutral taxiway (no flow rule)
    //   yellow — taxilane (~3×)
    //   orange — holding-point connector (~25×), drawn bold
    //   red    — runway (≥30×)
    enum EdgeStyle
    {
        STYLE_GREEN,
        STYLE_WHITE,
        STYLE_YELLOW,
        STYLE_ORANGE_BOLD,
        STYLE_RED,
        STYLE_COUNT
    };
    HPEN stylePens[STYLE_COUNT];
    stylePens[STYLE_GREEN]       = CreatePen(PS_SOLID, 2, RGB(0, 200, 100));
    stylePens[STYLE_WHITE]       = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    stylePens[STYLE_YELLOW]      = CreatePen(PS_SOLID, 2, RGB(255, 220, 0));
    stylePens[STYLE_ORANGE_BOLD] = CreatePen(PS_SOLID, 5, RGB(255, 130, 0));
    stylePens[STYLE_RED]         = CreatePen(PS_SOLID, 2, RGB(220, 50, 50));

    HPEN prevPen  = static_cast<HPEN>(SelectObject(hDC, stylePens[STYLE_WHITE]));
    int  curStyle = STYLE_WHITE;

    for (int i = 0; i < static_cast<int>(adj.size()); ++i)
    {
        const POINT ptA = toPixel(nodes[i].pos);
        for (const auto& e : adj[i])
        {
            if (e.to <= i)
                continue; // draw each edge once

            const double distM = HaversineM(nodes[i].pos, nodes[e.to].pos);
            const double mult  = (distM > 0.01) ? e.cost / distM : 1.0;
            int          style;
            if (mult >= 30.0)
                style = STYLE_RED;
            else if (mult >= 5.0)
                style = STYLE_ORANGE_BOLD;
            else if (mult >= 2.0)
                style = STYLE_YELLOW;
            else if (!e.wayRef.empty() && flowRefs.contains(e.wayRef))
                style = STYLE_GREEN;
            else
                style = STYLE_WHITE;
            if (style != curStyle)
            {
                SelectObject(hDC, stylePens[style]);
                curStyle = style;
            }

            const POINT ptB = toPixel(nodes[e.to].pos);
            MoveToEx(hDC, ptA.x, ptA.y, nullptr);
            LineTo(hDC, ptB.x, ptB.y);
        }
    }
    SelectObject(hDC, prevPen);
    for (int b = 0; b < STYLE_COUNT; ++b)
        DeleteObject(stylePens[b]);

    // Pass 2 — nodes: 6 × 6 px square coloured by type.
    //   Waypoint       → cyan
    //   HoldingPosition→ red
    //   Stand          → green  (added lazily; only visible after a route to a stand is computed)
    //   HoldingPoint   → not drawn (config centroids; not useful in visual debug)
    for (const auto& n : nodes)
    {
        if (n.type == TaxiNodeType::HoldingPoint)
            continue;
        const COLORREF col = (n.type == TaxiNodeType::HoldingPosition) ? RGB(220, 50, 50)
                             : (n.type == TaxiNodeType::Stand)         ? RGB(50, 220, 50)
                                                                       : RGB(0, 200, 255);
        const POINT    pt  = toPixel(n.pos);
        HBRUSH         br  = CreateSolidBrush(col);
        const RECT     r   = {pt.x - 2, pt.y - 2, pt.x + 2, pt.y + 2};
        FillRect(hDC, &r, br);
        DeleteObject(br);
    }

    // Pass 3 — flow chevrons: one per directed edge that is "with flow" for the active config.
    // Each chevron is two short lines forming a ">" at the edge midpoint, pointing in the
    // direction of travel.  Only edges longer than MIN_PX pixels on screen are annotated to
    // avoid cluttering heavily-subdivided short segments.
    if (!activeFlowRules.empty())
    {
        // Cardinal direction → bearing (degrees).
        auto cardinalBearing = [](const std::string& dir) -> double
        {
            if (dir == "N")
                return 0.0;
            if (dir == "E")
                return 90.0;
            if (dir == "S")
                return 180.0;
            if (dir == "W")
                return 270.0;
            return -1.0;
        };

        constexpr double MIN_PX     = 8.0;  // skip chevron on very short screen segments
        constexpr double CHEVRON_S  = 8.0;  // half-arm length in pixels
        constexpr double SPACING_PX = 80.0; // minimum screen distance between chevrons on the same wayRef

        // Pre-build a map from wayRef → rule bearing for O(1) lookup inside the loop.
        std::map<std::string, double> refBearing;
        for (const auto& rule : activeFlowRules)
        {
            const double b = cardinalBearing(rule.direction);
            if (b >= 0.0)
                refBearing.emplace(rule.taxiway, b);
        }

        // Last midpoint (screen px) where a chevron was drawn, keyed by wayRef.
        std::map<std::string, POINT> lastChevron;

        HPEN chevronPen  = CreatePen(PS_SOLID, 2, RGB(255, 215, 50));
        HPEN prevChevPen = static_cast<HPEN>(SelectObject(hDC, chevronPen));

        for (int i = 0; i < static_cast<int>(adj.size()); ++i)
        {
            const POINT ptA = toPixel(nodes[i].pos);
            for (const auto& e : adj[i])
            {
                if (e.wayRef.empty())
                    continue;

                const auto rIt = refBearing.find(e.wayRef);
                if (rIt == refBearing.end())
                    continue;

                // Only annotate edges whose bearing is within 45° of the rule (i.e. with-flow).
                if (BearingDiff(e.bearingDeg, rIt->second) > 45.0)
                    continue;

                const POINT  ptB = toPixel(nodes[e.to].pos);
                const double dx  = static_cast<double>(ptB.x - ptA.x);
                const double dy  = static_cast<double>(ptB.y - ptA.y);
                const double len = std::sqrt(dx * dx + dy * dy);
                if (len < MIN_PX)
                    continue;

                // Enforce minimum spacing between chevrons on the same wayRef.
                const double mx = (ptA.x + ptB.x) / 2.0;
                const double my = (ptA.y + ptB.y) / 2.0;
                if (const auto lIt = lastChevron.find(e.wayRef); lIt != lastChevron.end())
                {
                    const double sdx = mx - lIt->second.x;
                    const double sdy = my - lIt->second.y;
                    if (std::sqrt(sdx * sdx + sdy * sdy) < SPACING_PX)
                        continue;
                }
                lastChevron[e.wayRef] = {static_cast<LONG>(mx), static_cast<LONG>(my)};

                // Unit direction and perpendicular.
                const double ux = dx / len;
                const double uy = dy / len;
                const double px = -uy;
                const double py = ux;

                const POINT apex  = {static_cast<LONG>(mx + ux * CHEVRON_S),
                                     static_cast<LONG>(my + uy * CHEVRON_S)};
                const POINT barbL = {static_cast<LONG>(mx - ux * CHEVRON_S + px * CHEVRON_S),
                                     static_cast<LONG>(my - uy * CHEVRON_S + py * CHEVRON_S)};
                const POINT barbR = {static_cast<LONG>(mx - ux * CHEVRON_S - px * CHEVRON_S),
                                     static_cast<LONG>(my - uy * CHEVRON_S - py * CHEVRON_S)};

                MoveToEx(hDC, barbL.x, barbL.y, nullptr);
                LineTo(hDC, apex.x, apex.y);
                LineTo(hDC, barbR.x, barbR.y);
            }
        }

        SelectObject(hDC, prevChevPen);
        DeleteObject(chevronPen);
    }
}

void RadarScreen::DrawTaxiConflicts(HDC hDC)
{
    if (this->taxiConflicts.empty())
        return;

    HPEN prevPen = static_cast<HPEN>(GetCurrentObject(hDC, OBJ_PEN));

    for (const auto& c : this->taxiConflicts)
    {
        const double tMin = std::min(c.tA, c.tB);
        COLORREF     col;
        if (tMin < 15.0)
            col = RGB(220, 50, 50);
        else if (tMin < 30.0)
            col = RGB(255, 220, 0);
        else
            continue; // too far ahead — no marker yet

        EuroScopePlugIn::CPosition isxCPos;
        isxCPos.m_Latitude  = c.pt.lat;
        isxCPos.m_Longitude = c.pt.lon;
        const POINT px      = ConvertCoordFromPositionToPixel(isxCPos);

        HPEN mPen = CreatePen(PS_SOLID, 2, col);
        SelectObject(hDC, mPen);
        MoveToEx(hDC, px.x - 5, px.y - 5, nullptr);
        LineTo(hDC, px.x + 5, px.y + 5);
        MoveToEx(hDC, px.x + 5, px.y - 5, nullptr);
        LineTo(hDC, px.x - 5, px.y + 5);
        SelectObject(hDC, prevPen);
        DeleteObject(mPen);
    }
}

void RadarScreen::DrawTaxiWarningLabels(HDC hDC)
{
    if (this->taxiDeviations.empty() && this->taxiConflicts.empty())
        return;

    HFONT warnFont = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, "EuroScope");
    HFONT prevFont = static_cast<HFONT>(SelectObject(hDC, warnFont));
    SetBkMode(hDC, TRANSPARENT);

    for (auto rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt      = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        auto fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
            continue;
        const std::string cs = fp.GetCallsign();

        // Three tiers: CRITICAL (<15 s) > WARN (15–30 s) > deviation.
        enum class Tier
        {
            None,
            Deviation,
            Warn,
            Critical
        } tier = Tier::None;

        for (const auto& c : this->taxiConflicts)
        {
            if (c.csA != cs && c.csB != cs)
                continue;
            const double tMin = std::min(c.tA, c.tB);
            if (tMin < 15.0)
            {
                tier = Tier::Critical;
                break;
            }
            if (tMin < 30.0 && tier < Tier::Warn)
                tier = Tier::Warn;
        }
        if (tier == Tier::None && this->taxiDeviations.contains(cs))
            tier = Tier::Deviation;
        if (tier == Tier::None)
            continue;

        const char*    label   = (tier == Tier::Deviation) ? "!ROUTE" : "CONFLICT";
        const COLORREF bgCol   = (tier == Tier::Critical) ? TAG_COLOR_RED : TAG_COLOR_YELLOW;
        const COLORREF textCol = (tier == Tier::Critical) ? RGB(255, 255, 255) : RGB(0, 0, 0);

        const POINT acPt = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());

        RECT m = {0, 0, 200, 30};
        DrawTextA(hDC, label, -1, &m, DT_CALCRECT | DT_SINGLELINE);
        constexpr int PAD = 1;
        constexpr int GAP = 14;

        const RECT bgRect = {acPt.x - m.right / 2 - PAD,
                             acPt.y - GAP - m.bottom - PAD * 2,
                             acPt.x + m.right / 2 + PAD,
                             acPt.y - GAP};

        HBRUSH brush = CreateSolidBrush(bgCol);
        FillRect(hDC, &bgRect, brush);
        DeleteObject(brush);

        SetTextColor(hDC, textCol);
        RECT textRect = bgRect;
        DrawTextA(hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hDC, prevFont);
    DeleteObject(warnFont);
}

/// @brief Highlights dead-end taxilane branches that are isolated by active push routes.
/// Called from BEFORE_TAGS only when in taxi planning mode (not push planning).
void RadarScreen::DrawPushDeadEnds(HDC hDC)
{
    if (this->taxiPlanIsPush || this->pushTracked.empty())
        return;

    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->osmGraph.IsBuilt())
        return;

    // Collect all blocked nodes from active push routes.
    std::set<int> blocked;
    for (const auto& [_, pushRoute] : this->pushTracked)
    {
        auto b = settings->osmGraph.NodesToBlock(pushRoute.polyline, 3.0);
        blocked.insert(b.begin(), b.end());
    }
    if (blocked.empty())
        return;

    // Re-derive planning destination (same logic as the right-click trigger).
    auto        replanAptIt = settings->FindMyAirport();
    std::string ourIcao     = (replanAptIt != settings->GetAirports().end()) ? replanAptIt->first : std::string{};

    auto fp = GetPlugIn()->FlightPlanSelect(this->taxiPlanActive.c_str());
    auto rt = GetPlugIn()->RadarTargetSelect(this->taxiPlanActive.c_str());
    if (!fp.IsValid() || !rt.IsValid())
        return;

    std::string arrAirport = fp.GetFlightPlanData().GetDestination();
    to_upper(arrAirport);
    const bool isInbound = (!ourIcao.empty() && arrAirport == ourIcao);

    GeoPoint dest{0.0, 0.0};
    if (isInbound)
    {
        auto* timers  = static_cast<CFlowX_Timers*>(this->GetPlugIn());
        auto  standIt = timers->GetStandAssignment().find(this->taxiPlanActive);
        if (standIt != timers->GetStandAssignment().end())
        {
            const std::string& standName = standIt->second;
            std::string        standKey  = ourIcao + ":" + standName;
            const auto&        ap        = replanAptIt->second;
            auto               ovIt      = ap.standRoutingTargets.find(standName);
            if (ovIt != ap.standRoutingTargets.end())
            {
                const auto& srt = ovIt->second;
                if (srt.type == standRoutingTarget::Type::stand)
                {
                    dest = TaxiGraph::StandApproachPoint(ourIcao + ":" + srt.target, settings->GetGrStands());
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
        }
    }
    else if (replanAptIt != settings->GetAirports().end())
    {
        // FP runway takes priority; fall back to controller's active config.
        std::set<std::string> rwySearch;
        if (fp.IsValid())
        {
            std::string fpRwy = fp.GetFlightPlanData().GetDepartureRwy();
            if (!fpRwy.empty())
                rwySearch.insert(fpRwy);
        }
        if (rwySearch.empty())
            rwySearch = settings->GetActiveDepRunways();
        dest = settings->osmGraph.BestDepartureHP(rwySearch, replanAptIt->second);
    }
    if (dest.lat == 0.0 && dest.lon == 0.0)
        return; // no destination to check

    auto deadEdges = settings->osmGraph.DeadEndEdges(dest, blocked);
    if (deadEdges.empty())
        return;

    HPEN redPen  = CreatePen(PS_SOLID, 3, RGB(220, 50, 50));
    HPEN prevPen = static_cast<HPEN>(SelectObject(hDC, redPen));
    for (const auto& [a, b] : deadEdges)
    {
        EuroScopePlugIn::CPosition pa, pb;
        pa.m_Latitude   = a.lat;
        pa.m_Longitude  = a.lon;
        pb.m_Latitude   = b.lat;
        pb.m_Longitude  = b.lon;
        const POINT pta = ConvertCoordFromPositionToPixel(pa);
        const POINT ptb = ConvertCoordFromPositionToPixel(pb);
        MoveToEx(hDC, pta.x, pta.y, nullptr);
        LineTo(hDC, ptb.x, ptb.y);
    }
    SelectObject(hDC, prevPen);
    DeleteObject(redPen);
}
