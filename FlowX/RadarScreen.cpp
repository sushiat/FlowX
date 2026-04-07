/**
 * @file RadarScreen.cpp
 * @brief Radar screen implementation; GDI rendering, drag interaction, and controller station tracking.
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
#include <mmsystem.h>
#include <string>
#include "constants.h"
#include "helpers.h"
#include "osm_taxiways.h"

RadarScreen::RadarScreen()
{
    this->debug = false;
}

RadarScreen::~RadarScreen() = default;

/// @brief Notifies the plugin that the screen is closing, then deletes this RadarScreen.
void RadarScreen::OnAsrContentToBeClosed()
{
    static_cast<CFlowX_Base*>(GetPlugIn())->ClearRadarScreen();
    delete this;
}

/// @brief Adds the controller to the appropriate station set when they come online or update their position.
/// @param Controller The updated controller.
void RadarScreen::OnControllerPositionUpdate(EuroScopePlugIn::CController Controller)
{
    try
    {
        std::string cs = Controller.GetCallsign();
        std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

        std::string myCS = this->GetPlugIn()->ControllerMyself().GetCallsign();
        std::transform(myCS.begin(), myCS.end(), myCS.begin(), ::toupper);

        // Not interested in observers, non-controllers and my own call-sign
        if (Controller.IsController() && Controller.GetRating() > 1 && cs != myCS)
        {
            double      freq          = Controller.GetPrimaryFrequency();
            std::string freqFormatted = std::format("{:.3f}", freq);

            auto updateStation = [&](std::map<std::string, std::string>& stations, const char* label) {
                auto it = stations.find(cs);
                if (it == stations.end())
                {
                    stations.emplace(cs, freqFormatted);
                    if (this->debug)
                    {
                        this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, label, (cs + " online (" + freqFormatted + ")").c_str(), true, true, true, false, false);
                    }
                }
                else if (it->second != freqFormatted)
                {
                    if (this->debug)
                    {
                        this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, label, (cs + " freq changed: " + it->second + " -> " + freqFormatted).c_str(), true, true, true, false, false);
                    }
                    it->second = freqFormatted;
                }
            };

            if (Controller.GetFacility() == 3)
            {
                updateStation(this->groundStations, "Ground");
            }
            if (Controller.GetFacility() == 4 && cs.find("ATIS") == std::string::npos)
            {
                updateStation(this->towerStations, "Tower");
            }
            if (Controller.GetFacility() == 5)
            {
                updateStation(this->approachStations, "Approach");
            }
            if (Controller.GetFacility() == 6)
            {
                updateStation(this->centerStations, "Center");
            }
        }
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnControllerPositionUpdate", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnControllerPositionUpdate", "unknown exception"); }
}

/// @brief Removes the controller from the appropriate station set when they go offline.
/// @param Controller The disconnected controller.
void RadarScreen::OnControllerDisconnect(EuroScopePlugIn::CController Controller)
{
    try
    {
        std::string cs = Controller.GetCallsign();
        std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

        // Not interested in observers and non-controllers
        if (Controller.IsController() && Controller.GetRating() > 1)
        {
            if (Controller.GetFacility() == 3)
            {
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Ground", (cs + " disconnected").c_str(), true, true, true, false, false);
                }

                if (this->groundStations.find(cs) != this->groundStations.end())
                {
                    this->groundStations.erase(cs);
                }
            }

            if (Controller.GetFacility() == 4 && cs.find("ATIS") == std::string::npos)
            {
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Tower", (cs + " disconnected").c_str(), true, true, true, false, false);
                }

                if (this->towerStations.find(cs) != this->towerStations.end())
                {
                    this->towerStations.erase(cs);
                }
            }

            if (Controller.GetFacility() == 5)
            {
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Approach", (cs + " disconnected").c_str(), true, true, true, false, false);
                }

                if (this->approachStations.find(cs) != this->approachStations.end())
                {
                    this->approachStations.erase(cs);
                }
            }

            if (Controller.GetFacility() == 6)
            {
                if (this->debug)
                {
                    this->GetPlugIn()->DisplayUserMessage(PLUGIN_NAME, "Center", (cs + " disconnected").c_str(), true, true, true, false, false);
                }

                if (this->centerStations.find(cs) != this->centerStations.end())
                {
                    this->centerStations.erase(cs);
                }
            }
        }
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnControllerDisconnect", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnControllerDisconnect", "unknown exception"); }
}

/// @brief Dispatches all custom drawing to the four extract draw helpers; no calculations here.
/// @param hDC GDI device context for drawing.
/// @param Phase EuroScope refresh phase; all drawing occurs during REFRESH_PHASE_AFTER_TAGS.
void RadarScreen::OnRefresh(HDC hDC, int Phase)
{
    try
    {
        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS)
        {
            auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());

            this->DrawDepartureInfoTag(hDC);
            this->DrawGndTransferSquares(hDC);

            // Draw taxiway/taxilane overlay when enabled.
            if (this->showTaxiOverlay && !settings->osmData.ways.empty())
            {
                const bool showLanes = settings->GetOsmShowTaxilanes();

                // Pass 1 — draw polylines, one pen per way.
                for (const auto& way : settings->osmData.ways)
                {
                    if (way.type == AerowayType::Taxilane && !showLanes) continue;

                    const COLORREF col = (way.type == AerowayType::Taxiway_HoldingPoint) ? RGB(255,  80,  80)
                                       : (way.type == AerowayType::Taxilane)             ? RGB(  0, 200, 255)
                                                                                          : RGB(255, 220,   0);
                    HPEN pen  = CreatePen(PS_SOLID, 2, col);
                    HPEN prev = static_cast<HPEN>(SelectObject(hDC, pen));

                    bool first = true;
                    for (const auto& gp : way.geometry)
                    {
                        EuroScopePlugIn::CPosition pos;
                        pos.m_Latitude  = gp.lat;
                        pos.m_Longitude = gp.lon;
                        const POINT pt = ConvertCoordFromPositionToPixel(pos);
                        if (first) { MoveToEx(hDC, pt.x, pt.y, nullptr); first = false; }
                        else         LineTo(hDC, pt.x, pt.y);
                    }

                    SelectObject(hDC, prev);
                    DeleteObject(pen);
                }

                // Pass 2 — draw name labels at 500 m intervals (first at 250 m).
                HFONT labelFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                HFONT prevFont  = static_cast<HFONT>(SelectObject(hDC, labelFont));
                SetBkMode(hDC, TRANSPARENT);

                for (const auto& way : settings->osmData.ways)
                {
                    if (way.type == AerowayType::Taxilane && !showLanes) continue;
                    if (way.geometry.size() < 2) continue;

                    const std::string& lbl = way.ref.empty() ? way.name : way.ref;
                    if (lbl.empty()) continue;

                    const COLORREF textCol = (way.type == AerowayType::Taxiway_HoldingPoint) ? RGB(255, 130, 130)
                                           : (way.type == AerowayType::Taxilane)             ? RGB(150, 230, 255)
                                                                                              : RGB(255, 240, 150);
                    SetTextColor(hDC, textCol);

                    double accum   = 0.0;
                    double nextLbl = 250.0;
                    bool   placed  = false;

                    for (size_t k = 1; k < way.geometry.size(); ++k)
                    {
                        const auto& a = way.geometry[k - 1];
                        const auto& b = way.geometry[k];
                        const double dLat = (b.lat - a.lat) * std::numbers::pi / 180.0;
                        const double dLon = (b.lon - a.lon) * std::numbers::pi / 180.0;
                        const double cosA = std::cos(a.lat * std::numbers::pi / 180.0);
                        const double cosB = std::cos(b.lat * std::numbers::pi / 180.0);
                        const double h    = std::sin(dLat / 2) * std::sin(dLat / 2)
                                          + cosA * cosB * std::sin(dLon / 2) * std::sin(dLon / 2);
                        const double seg  = 6'371'000.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));

                        if (accum + seg >= nextLbl)
                        {
                            EuroScopePlugIn::CPosition pos;
                            pos.m_Latitude  = b.lat;
                            pos.m_Longitude = b.lon;
                            const POINT pt = ConvertCoordFromPositionToPixel(pos);
                            RECT tr = { pt.x - 15, pt.y - 6, pt.x + 15, pt.y + 6 };
                            DrawTextA(hDC, lbl.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            nextLbl += 500.0;
                            placed   = true;
                        }
                        accum += seg;
                    }

                    // Way shorter than 250 m: one label at the middle node.
                    if (!placed)
                    {
                        const size_t mid = way.geometry.size() / 2;
                        EuroScopePlugIn::CPosition pos;
                        pos.m_Latitude  = way.geometry[mid].lat;
                        pos.m_Longitude = way.geometry[mid].lon;
                        const POINT pt = ConvertCoordFromPositionToPixel(pos);
                        RECT tr = { pt.x - 15, pt.y - 6, pt.x + 15, pt.y + 6 };
                        DrawTextA(hDC, lbl.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }

                SelectObject(hDC, prevFont);
                DeleteObject(labelFont);
            }
        }

        if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS)
        {
            this->DrawApproachEstimateWindow(hDC);
            this->DrawDepRateWindow(hDC);
            this->DrawTwrOutbound(hDC);
            this->DrawTwrInbound(hDC);
            this->DrawNapReminder(hDC);
            this->DrawWeatherWindow(hDC);
            this->DrawStartButton(hDC);
            this->DrawStartMenu(hDC);
        }
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnRefresh", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnRefresh", "unknown exception"); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private draw helpers — pure GDI output, no state calculations
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Draws a green filled square near each landed inbound awaiting GND handoff; registers each as a clickable screen object.
void RadarScreen::DrawGndTransferSquares(HDC hDC)
{
    if (this->gndTransferSquares.empty()) { return; }

    SetBkMode(hDC, TRANSPARENT);
    ULONGLONG now = GetTickCount64();

    for (const auto& callSign : this->gndTransferSquares)
    {
        EuroScopePlugIn::CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callSign.c_str());
        if (!rt.IsValid() || !rt.GetPosition().IsValid()) { continue; }

        ULONGLONG elapsedSec = this->gndTransferSquareTimes.contains(callSign)
                             ? (now - this->gndTransferSquareTimes.at(callSign)) / 1000
                             : 0;
        COLORREF color = elapsedSec >= 35 ? TAG_COLOR_RED
                       : elapsedSec >= 25 ? TAG_COLOR_YELLOW
                                          : TAG_COLOR_GREEN;

        auto brush = CreateSolidBrush(color);
        auto pen   = CreatePen(PS_SOLID, 1, color);
        SelectObject(hDC, brush);
        SelectObject(hDC, pen);

        POINT pt = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
        RECT  sq = { pt.x - 22, pt.y + 10, pt.x - 10, pt.y + 22 };
        Rectangle(hDC, sq.left, sq.top, sq.right, sq.bottom);
        AddScreenObject(SCREEN_OBJECT_GND_TRANSFER, callSign.c_str(), sq, true, "");

        DeleteObject(pen);
        DeleteObject(brush);
    }
}

static void FillRectAlpha(HDC hDC, const RECT& rect, COLORREF color, int opacityPct); ///< Forward declaration — defined below DrawGndTransferSquares.

/// @brief Draws the Approach Estimate window: two vertical bars with a 300→0 s scale between them.
/// Left-side runways place their aircraft to the left of the left bar; right-side to the right of the right bar.
/// Runway labels appear at the top corners of the header row. Ticks every 5 s (short) and 10 s (long); labels every 20 s.
void RadarScreen::DrawApproachEstimateWindow(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->GetApproachEstVisible()) { return; }

    const int fo        = settings->GetFontOffset();
    const int op        = settings->GetBgOpacity();
    const int TITLE_H   = 13;
    const int HDR_H     = 14 + fo;  ///< Header row carrying the runway corner labels
    const int RESIZE_SZ = 8;        ///< Size of the draggable resize square
    const int X_BTN     = 11;       ///< Close button size
    const int NUM_COL_W = 48;       ///< Fixed width of the numbers column between the two bars
    const int TICK_LONG = 10;       ///< Tick length (px into number column) for every-10-s marks
    const int TICK_SHORT= 8;        ///< Tick length for every-5-s (non-10-s) marks
    const int AC_PAD    = 4;        ///< Gap between the bar line and aircraft label text

    int WIN_W = this->approachEstWindowW;
    int WIN_H = this->approachEstWindowH;

    if (this->approachEstWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->approachEstWindowPos.x = clip.right  - WIN_W - 20;
        this->approachEstWindowPos.y = (clip.top + clip.bottom) / 2 - WIN_H / 2;
    }

    int wx = this->approachEstWindowPos.x;
    int wy = this->approachEstWindowPos.y;

    // ── Geometry ──
    // Two bars flank a fixed-width numbers column, centred in the window.
    int barLx     = wx + (WIN_W - NUM_COL_W) / 2;
    int barRx     = barLx + NUM_COL_W;
    int numCenterX = barLx + NUM_COL_W / 2;

    int barTop    = wy + TITLE_H + HDR_H + 4;
    int barBottom = wy + WIN_H - RESIZE_SZ - 4;
    if (barBottom <= barTop) { barBottom = barTop + 1; }

    // Scale padding: inset the usable 300→0 range so aircraft text at the extremes stays readable.
    // Top pad: small margin above the 300-s marker line.
    // Bottom pad: enough for callsign + type below the 0-s marker line.
    const int SCALE_PAD_TOP    = 34;
    const int SCALE_PAD_BOTTOM = 34;
    int scaleTop    = barTop    + SCALE_PAD_TOP;
    int scaleBottom = barBottom - SCALE_PAD_BOTTOM;
    if (scaleBottom <= scaleTop) { scaleBottom = scaleTop + 1; }
    int barHeight = scaleBottom - scaleTop;

    // ── Close button hover ──
    RECT xRect    = { wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN };
    HWND hwnd     = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd) { ScreenToClient(hwnd, &cursor); }
    bool xHovered = PtInRect(&xRect, cursor) != 0;

    // ── Background ──
    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H           };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H         };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H };

    FillRectAlpha(hDC, winRect,  RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect,  RGB(40, 40, 40), op);

    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // ── Title bar ──
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "APPROACH ESTIMATE", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);

    RECT dragRect = { wx, wy, wx + WIN_W - X_BTN - 2, wy + TITLE_H };
    AddScreenObject(SCREEN_OBJECT_APPROACH_EST_WIN, "APPROACH_EST", dragRect, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE,        "approachEst",  xRect,    false, "");

    // ── Runway corner labels — always from config, not from active inbounds ──
    auto [leftLabel, rightLabel] = settings->GetEstimateBarLabels();

    // ── Header row: runway labels at top corners ──
    HFONT hdrFont = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_DEFAULT_GRAY);
    if (!leftLabel.empty())
    {
        RECT lr = { wx + 3, wy + TITLE_H, wx + WIN_W / 2, wy + TITLE_H + HDR_H };
        DrawTextA(hDC, leftLabel.c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!rightLabel.empty())
    {
        RECT rr = { wx + WIN_W / 2, wy + TITLE_H, wx + WIN_W - 3, wy + TITLE_H + HDR_H };
        DrawTextA(hDC, rightLabel.c_str(), -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFont);
    DeleteObject(hdrFont);

    // ── Two vertical bars ──
    auto barPen  = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    HGDIOBJ prevPen = SelectObject(hDC, barPen);
    MoveToEx(hDC, barLx, barTop, nullptr); LineTo(hDC, barLx, barBottom);
    MoveToEx(hDC, barRx, barTop, nullptr); LineTo(hDC, barRx, barBottom);
    SelectObject(hDC, prevPen);
    DeleteObject(barPen);

    // ── Tick marks and labels ──
    // Every 5 s: short tick inward from each bar; every 10 s: longer; labels every 20 s.
    HFONT tickFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont = (HFONT)SelectObject(hDC, tickFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    auto tickPen = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    prevPen = SelectObject(hDC, tickPen);

    for (int t = 300; t >= 0; t -= 5)
    {
        int ty      = scaleTop + (300 - t) * barHeight / 300;
        int tickLen = (t % 10 == 0) ? TICK_LONG : TICK_SHORT;

        // Left bar: tick extends rightward (into numbers column)
        MoveToEx(hDC, barLx, ty, nullptr);
        LineTo(hDC, barLx + tickLen, ty);
        // Right bar: tick extends leftward (into numbers column)
        MoveToEx(hDC, barRx, ty, nullptr);
        LineTo(hDC, barRx - tickLen, ty);

        // Label every 20 s, centred in the numbers column
        if (t % 20 == 0)
        {
            std::string lbl = std::to_string(t);
            SIZE sz = {};
            GetTextExtentPoint32A(hDC, lbl.c_str(), (int)lbl.size(), &sz);
            TextOutA(hDC, numCenterX - sz.cx / 2, ty - sz.cy / 2, lbl.c_str(), (int)lbl.size());
        }
    }

    SelectObject(hDC, prevPen);
    DeleteObject(tickPen);
    SelectObject(hDC, prevFont);
    DeleteObject(tickFont);

    // ── Aircraft labels ──
    HFONT csFont = CreateFontA(-16 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT atFont = CreateFontA(-13 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    bool useColors = settings->GetApprEstColors();

    auto markerPen = CreatePen(PS_SOLID, 1, TAG_COLOR_GREEN);

    for (const auto& row : this->twrInboundRowsCache)
    {
        if (row.tttSeconds < 0 || row.tttSeconds > 300) { continue; }
        if (row.rwy.empty()) { continue; }

        std::string side = settings->GetRunwayEstimateBarSide(row.rwy);
        if (side != "left" && side != "right") { continue; }

        int clampedTTT = std::clamp(row.tttSeconds, 0, 300);
        int y          = scaleTop + (300 - clampedTTT) * barHeight / 300;
        y              = std::clamp(y, scaleTop, scaleBottom);

        // Determine colors — either always-green or inbound-list colors
        COLORREF csColor   = TAG_COLOR_GREEN;
        COLORREF bgColor   = TAG_COLOR_DEFAULT_NONE;
        COLORREF textOnBg  = TAG_COLOR_GREEN;
        if (useColors)
        {
            if (row.isGoAround)
            {
                csColor  = TAG_COLOR_WHITE;
                bgColor  = TAG_BG_COLOR_RED;
                textOnBg = TAG_COLOR_WHITE;
            }
            else if (row.isFrozen)
            {
                csColor  = TAG_COLOR_BLACK;
                bgColor  = TAG_BG_COLOR_YELLOW;
                textOnBg = TAG_COLOR_BLACK;
            }
            else
            {
                csColor  = row.callsignColor;
                bgColor  = TAG_COLOR_DEFAULT_NONE;
                textOnBg = row.callsignColor;
            }
        }

        // Horizontal marker line — always green, same length as the 10-s tick (TICK_LONG)
        prevPen = SelectObject(hDC, markerPen);
        if (side == "left")
        {
            MoveToEx(hDC, barLx, y, nullptr);
            LineTo(hDC, barLx - TICK_LONG, y);
        }
        else
        {
            MoveToEx(hDC, barRx, y, nullptr);
            LineTo(hDC, barRx + TICK_LONG, y);
        }
        SelectObject(hDC, prevPen);

        // Callsign text — sits just below the marker line, adjacent to the bar
        prevFont = (HFONT)SelectObject(hDC, csFont);
        SIZE csSize = {};
        GetTextExtentPoint32A(hDC, row.callsign.c_str(), (int)row.callsign.size(), &csSize);
        int csX = (side == "left") ? barLx - AC_PAD - csSize.cx : barRx + AC_PAD;
        int csY = y + 1;

        if (bgColor != TAG_COLOR_DEFAULT_NONE)
        {
            RECT fillRect = { (LONG)(csX - 2), (LONG)(csY - 1), (LONG)(csX + csSize.cx + 2), (LONG)(csY + csSize.cy + 1) };
            HBRUSH bgBrush = CreateSolidBrush(bgColor);
            FillRect(hDC, &fillRect, bgBrush);
            DeleteObject(bgBrush);
            SetTextColor(hDC, textOnBg);
        }
        else
        {
            SetTextColor(hDC, csColor);
        }
        TextOutA(hDC, csX, csY, row.callsign.c_str(), (int)row.callsign.size());
        SelectObject(hDC, prevFont);

        // Aircraft type — below the callsign, no background box
        if (!row.aircraftType.empty())
        {
            prevFont = (HFONT)SelectObject(hDC, atFont);
            SetTextColor(hDC, csColor);
            SIZE atSize = {};
            GetTextExtentPoint32A(hDC, row.aircraftType.c_str(), (int)row.aircraftType.size(), &atSize);
            int atX = (side == "left") ? barLx - AC_PAD - atSize.cx : barRx + AC_PAD;
            TextOutA(hDC, atX, csY + csSize.cy - 3, row.aircraftType.c_str(), (int)row.aircraftType.size());
            SelectObject(hDC, prevFont);
        }
    }

    DeleteObject(markerPen);
    DeleteObject(csFont);
    DeleteObject(atFont);

    // ── Resize handle (draggable grey square, lower-right corner) ──
    RECT resizeRect = { wx + WIN_W - RESIZE_SZ, wy + WIN_H - RESIZE_SZ, wx + WIN_W, wy + WIN_H };
    auto resizeBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hDC, &resizeRect, resizeBrush);
    DeleteObject(resizeBrush);
    AddScreenObject(SCREEN_OBJECT_APPROACH_EST_RESIZE, "APPROACH_EST_RESIZE", resizeRect, true, "");
}

/// @brief Draws per-aircraft departure info overlays (text, SID dot, HP label, connector line).
void RadarScreen::DrawDepartureInfoTag(HDC hDC)
{
    SetBkMode(hDC, TRANSPARENT);

    for (auto& [cs, info] : this->radarTargetDepartureInfos)
    {
        if (info.pos.x <= -1 || info.pos.y <= -1) { continue; }

        SetTextColor(hDC, info.dep_color);
        SIZE textSize;
        GetTextExtentPoint32A(hDC, info.dep_info.c_str(), (int)info.dep_info.length(), &textSize);
        TextOutA(hDC,
                 info.pos.x - textSize.cx + info.dragX,
                 info.pos.y + info.dragY,
                 info.dep_info.c_str(), (int)info.dep_info.length());

        RECT area;
        area.left   = info.pos.x - textSize.cx - 2 + info.dragX;
        area.top    = info.pos.y - 2 + info.dragY;
        area.right  = info.pos.x + 2 + info.dragX;
        area.bottom = info.pos.y + textSize.cy + 2 + info.dragY;

        auto sidBrush = CreateSolidBrush(info.sid_color);
        auto sidPen   = CreatePen(PS_SOLID, 1, info.sid_color);
        SelectObject(hDC, sidBrush);
        SelectObject(hDC, sidPen);
        RECT dotRect = {
            info.pos.x - textSize.cx + info.dragX + 2,
            info.pos.y + info.dragY + (area.bottom - area.top) - 5 + 2,
            info.pos.x - textSize.cx + info.dragX + 14,
            info.pos.y + info.dragY + (area.bottom - area.top) - 5 + 14 };
        Ellipse(hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
        DeleteObject(sidBrush);
        DeleteObject(sidPen);

        if (!info.hp_info.empty())
        {
            SetTextColor(hDC, info.hp_color);
            TextOutA(hDC,
                     info.pos.x - textSize.cx + 18 + info.dragX,
                     info.pos.y + info.dragY + (area.bottom - area.top) - 5,
                     info.hp_info.c_str(), (int)info.hp_info.length());
            area.bottom += (area.bottom - area.top) - 5;
        }

        auto pen = CreatePen(PS_SOLID, 1, info.dep_color);
        SelectObject(hDC, pen);
        MoveToEx(hDC, info.pos.x + 16, info.pos.y - 3, nullptr);
        if (area.right <= info.pos.x + 16)
            LineTo(hDC, area.right, area.top + (area.bottom - area.top) / 2);
        else
            LineTo(hDC, area.left,  area.top + (area.bottom - area.top) / 2);
        DeleteObject(pen);

        if (info.queue_pos > 0)
        {
            std::string seqStr    = std::to_string(info.queue_pos);
            HFONT seqFont = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            HFONT prevSeqFont = (HFONT)SelectObject(hDC, seqFont);
            SIZE seqSize = {};
            GetTextExtentPoint32A(hDC, seqStr.c_str(), (int)seqStr.length(), &seqSize);
            constexpr int PAD     = 3;
            constexpr int GAP     = 14; // distance from radar target dot to bottom of background box
            int targetX = info.pos.x + 16;
            int targetY = info.pos.y - 3;
            int boxLeft   = targetX - seqSize.cx / 2 - PAD;
            int boxTop    = targetY - GAP - seqSize.cy - PAD * 2;
            int boxRight  = targetX + seqSize.cx / 2 + PAD + (seqSize.cx % 2 == 0 ? 0 : 1);
            int boxBottom = targetY - GAP;
            RECT bgRect   = { boxLeft, boxTop, boxRight, boxBottom };
            auto bgBrush  = CreateSolidBrush(RGB(50, 50, 50));
            FillRect(hDC, &bgRect, bgBrush);
            DeleteObject(bgBrush);
            SetTextColor(hDC, info.dep_color);
            SetBkMode(hDC, TRANSPARENT);
            TextOutA(hDC, boxLeft + PAD, boxTop + PAD, seqStr.c_str(), (int)seqStr.length());
            SelectObject(hDC, prevSeqFont);
            DeleteObject(seqFont);
        }

        AddScreenObject(SCREEN_OBJECT_DEP_TAG, cs.c_str(), area, true, "");
        AddScreenObject(SCREEN_OBJECT_DEP_TAG_SID_DOT, (cs + "|SID_DOT").c_str(), dotRect, false, "");
    }
}

/// @brief Fills a rect with the given color at the specified opacity percent (20–100).
/// At 100% falls back to a plain FillRect. Otherwise uses AlphaBlend (Msimg32).
static void FillRectAlpha(HDC hDC, const RECT& rect, COLORREF color, int opacityPct)
{
    int w = rect.right  - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) { return; }
    if (opacityPct >= 100)
    {
        auto brush = CreateSolidBrush(color);
        FillRect(hDC, &rect, brush);
        DeleteObject(brush);
        return;
    }
    HDC     memDC  = CreateCompatibleDC(hDC);
    HBITMAP bmp    = CreateCompatibleBitmap(hDC, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);
    RECT    local  = { 0, 0, w, h };
    auto    brush  = CreateSolidBrush(color);
    FillRect(memDC, &local, brush);
    DeleteObject(brush);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, static_cast<BYTE>(opacityPct * 255 / 100), 0 };
    AlphaBlend(hDC, rect.left, rect.top, w, h, memDC, 0, 0, w, h, bf);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

/// @brief Draws the DEP/H departure-rate window from the pre-calculated depRateRowsCache.
void RadarScreen::DrawDepRateWindow(HDC hDC)
{
    auto* settingsDep = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsDep->GetDepRateVisible()) { return; }
    int fo = settingsDep->GetFontOffset();
    int op = settingsDep->GetBgOpacity();

    const int TITLE_H = 13;
    const int HDR_H   = 17 + fo;
    const int ROW_H   = 15 + fo;
    const int WIN_PAD = 6;
    // Base column widths at font size 12; scaled by (12+fo)/12:
    auto cw = [&](int base) -> int { return base * (12 + fo) / 12; };
    const int COL_RWY = cw(35);
    const int COL_CNT = cw(20);
    const int COL_GAP = 6;
    const int COL_SPC = cw(56);
    const int WIN_W   = WIN_PAD + COL_RWY + COL_CNT + COL_GAP + COL_SPC + WIN_PAD;
    const int X_BTN   = 11;  ///< Width and height of the title-bar close button
    int numRows       = (int)this->depRateRowsCache.size();
    const int WIN_H   = TITLE_H + HDR_H + numRows * ROW_H + WIN_PAD / 2;

    if (this->depRateWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->depRateWindowPos.x = clip.right  - WIN_W - 20;
        this->depRateWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->depRateWindowPos.x;
    int wy = this->depRateWindowPos.y;

    // Close button rect (inside title bar, inset 1 px from border)
    RECT xRect = { wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN };
    this->winCloseLastHoverType = -1;
    HWND  hwndDep = WindowFromDC(hDC);
    POINT cursorDep;
    GetCursorPos(&cursorDep);
    if (hwndDep) { ScreenToClient(hwndDep, &cursorDep); }
    bool xHovered = PtInRect(&xRect, cursorDep) != 0;

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    FillRectAlpha(hDC, winRect,  RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect,  RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "Departures", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRect = { wx, wy, wx + WIN_W - X_BTN - 2, wy + TITLE_H };
    AddScreenObject(SCREEN_OBJECT_DEPRATE_WIN, "DEPRATE", dragRect, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "depRate", xRect, false, "");

    HFONT hdrFontDep  = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFontDep = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFontDep = (HFONT)SelectObject(hDC, hdrFontDep);

    // Column headers
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int colY0 = wy + TITLE_H;
        int splitX = wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP;
        RECT rwyHdr = { wx + WIN_PAD, colY0, wx + WIN_PAD + COL_RWY,         colY0 + HDR_H };
        RECT cntHdr = { wx + WIN_PAD + COL_RWY, colY0, splitX,               colY0 + HDR_H };
        RECT spcHdr = { splitX,       colY0, wx + WIN_W - WIN_PAD,            colY0 + HDR_H };
        DrawTextA(hDC, "RWY", -1, &rwyHdr, DT_LEFT  | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "#",   -1, &cntHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "AVG", -1, &spcHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Data rows
    SelectObject(hDC, dataFontDep);
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->depRateRowsCache[row];
        int rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + ROW_H }; FillRectAlpha(hDC, alt, RGB(32, 32, 32), op); }

        RECT rwyRect = { wx + WIN_PAD,                                rowY, wx + WIN_PAD + COL_RWY,                   rowY + ROW_H };
        RECT cntRect = { wx + WIN_PAD + COL_RWY,                      rowY, wx + WIN_PAD + COL_RWY + COL_CNT,         rowY + ROW_H };
        RECT spcRect = { wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP, rowY, wx + WIN_W - WIN_PAD,                     rowY + ROW_H };

        SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
        DrawTextA(hDC, r.runway.c_str(),     -1, &rwyRect, DT_LEFT  | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.countColor);
        DrawTextA(hDC, r.countStr.c_str(),   -1, &cntRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.spacingColor);
        DrawTextA(hDC, r.spacingStr.c_str(), -1, &spcRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFontDep);
    DeleteObject(hdrFontDep);
    DeleteObject(dataFontDep);
}

/// @brief Draws the TWR Outbound custom window from the pre-sorted twrOutboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrOutbound(HDC hDC)
{
    auto* settingsOut = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsOut->GetTwrOutboundVisible()) { return; }
    int fo = settingsOut->GetFontOffset();
    int op = settingsOut->GetBgOpacity();

    const int TITLE_H = 13;
    const int HDR_H   = 17 + fo;
    const int ROW_H   = 19 + fo;
    const int PAD     = 6;
    // Original list column order and widths in chars (base widths at font size 17; scaled by (17+fo)/17):
    // C/S=12, STS=12, DEP?=10, RWY=4, SID=11, WTC=4, ATYP=8, Freq=15, HP=4, Spacing=17, dNM=8
    auto cw = [&](int base) -> int { return base * (17 + fo) / 17; };
    const int CS      = cw(84);   // 12 chars
    const int STS     = cw(84);   // 12 chars
    const int DEP     = cw(70);   // 10 chars
    const int RWY     = cw(35);   //  4 chars (widened: "RWY" label needs more than 4×7px)
    const int SID     = cw(77);   // 11 chars
    const int WTC     = cw(28);   //  4 chars
    const int ATYP    = cw(56);   //  8 chars
    const int FREQ    = cw(105);  // 15 chars
    const int HP      = cw(28);   //  4 chars
    const int QPOS    = cw(21);   //  3 chars  (queue position: "1"–"9", or "10")
    const int SPC     = cw(119);  // 17 chars
    const int TMR     = cw(56);   //  8 chars ("9:59"; extra left margin separates from Spacing)
    const int LDST    = cw(77);   // 11 chars (live distance to previous departure; extra left margin separates from T+)
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H   = 12;    ///< Height of blank separator row between sort groups
    const int WIN_W   = PAD + CS + STS + DEP + RWY + SID + WTC + ATYP + FREQ + HP + QPOS + SPC + TMR + LDST + PAD;
    int numRows       = (int)this->twrOutboundRowsCache.size();

    int numSeps = 0;
    int totalRowH = 0;
    for (const auto& r : this->twrOutboundRowsCache)
    {
        if (r.groupSeparatorAbove) { ++numSeps; }
        totalRowH += r.dimmed ? DIMMED_ROW_H : ROW_H;
    }

    const int WIN_H = TITLE_H + HDR_H + totalRowH + numSeps * SEP_H + PAD / 2;

    if (this->twrOutboundWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->twrOutboundWindowPos.x = 20;
        this->twrOutboundWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->twrOutboundWindowPos.x;
    int wy = this->twrOutboundWindowPos.y;

    const int X_BTN = 11;
    RECT xRect = { wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN };
    this->winCloseLastHoverType = -1;
    HWND  hwndOut = WindowFromDC(hDC);
    POINT cursorOut;
    GetCursorPos(&cursorOut);
    if (hwndOut) { ScreenToClient(hwndOut, &cursorOut); }
    bool xHovered = PtInRect(&xRect, cursorOut) != 0;

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    FillRectAlpha(hDC, winRect,  RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect,  RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Outbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRectOut = { wx, wy, wx + WIN_W - X_BTN - 2, wy + TITLE_H };
    AddScreenObject(SCREEN_OBJECT_TWR_OUT_WIN, "TWROUT", dragRectOut, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "twrOut", xRect, false, "");

    // Column headers — smaller font
    HFONT hdrFont = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevDataFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int x = wx + PAD;
        int colY0 = wy + TITLE_H;
        auto colHdr = [&](int width, const char* label, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT r = { x, colY0, x + width, colY0 + HDR_H };
            DrawTextA(hDC, label, -1, &r, flags);
            x += width;
        };
        colHdr(CS,   "C/S");
        colHdr(STS,  "STS");
        colHdr(DEP,  "DEP?");
        colHdr(RWY,  "RWY",  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SID,  "SID");
        colHdr(WTC,  "W",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP, "ATYP");
        colHdr(FREQ, "Freq");
        colHdr(HP,   "HP",   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(QPOS, "#",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SPC,  "Spacing", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(TMR,  "T+",      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(LDST, "dNM",     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for active aircraft, dimmed for departed+untracked
    HFONT dataFont = CreateFontA(-17 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevDataFont = (HFONT)SelectObject(hDC, dataFont);
    auto wtcColor = [](char wtc) -> COLORREF {
        switch (wtc) {
            case 'L': return TAG_COLOR_TURQ;
            case 'H': return TAG_COLOR_ORANGE;
            case 'J': return TAG_COLOR_RED;
            default:  return TAG_COLOR_LIST_GRAY;
        }
    };

    int rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->twrOutboundRowsCache[row];
        int rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        if (r.groupSeparatorAbove)
        {
            RECT sepRect = { wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H };
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }

        int rowY = rowTop;
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + rowH }; FillRectAlpha(hDC, alt, RGB(32, 32, 32), op); }
        int cx   = wx + PAD;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        // Plain clickable cell for non-tagInfo data (callsign, wtc, aircraft type).
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_OUT_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        // tagInfo-aware variants: honour bgColor, bold, and fontDelta in addition to text and colour.
        // When bgColor is set the fill is measured to the actual text extent and drawn as an overlay
        // on top of the normal row background, then text is redrawn on top of the fill.
        auto cellTag = [&](int width, const tagInfo& t, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont = CreateFontA(baseSize - delta, 0, 0, 0,
                                       t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int textLeft = (flags & DT_RIGHT)  ? rect.right - textSize.cx
                             : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                             :                       rect.left;
                int textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT fillRect = { (LONG)(textLeft - 3),          std::max(rect.top,    (LONG)(textTop - 1)),
                                  (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1)) };
                HBRUSH brush = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled) { SelectObject(hDC, r.dimmed ? dimFont : dataFont); DeleteObject(cellFont); }
            cx += width;
        };

        auto cellTagClickable = [&](int width, const tagInfo& t, const std::string& objId,
                                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont = CreateFontA(baseSize - delta, 0, 0, 0,
                                       t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int textLeft = (flags & DT_RIGHT)  ? rect.right - textSize.cx
                             : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                             :                       rect.left;
                int textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT fillRect = { (LONG)(textLeft - 3),          std::max(rect.top,    (LONG)(textTop - 1)),
                                  (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1)) };
                HBRUSH brush = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled) { SelectObject(hDC, r.dimmed ? dimFont : dataFont); DeleteObject(cellFont); }
            AddScreenObject(SCREEN_OBJECT_TWR_OUT_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(CS,   r.callsign,             r.callsignColor, r.callsign + "|CS");
        cellTagClickable(STS,  r.status,   r.callsign + "|STS");
        cellTagClickable(DEP,  r.depInfo,  r.callsign + "|DEP");
        cellTagClickable(RWY,  r.rwy,      r.callsign + "|RWY",  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(SID,  r.sameSid,  r.callsign + "|SID");
        cellTagClickable(WTC, { .tag = std::string(1, r.wtc), .color = wtcColor(r.wtc), .bold = (r.wtc != 'M' && r.wtc != ' '), .fontDelta = (r.wtc != 'M' && r.wtc != ' ') ? 1 : 0 }, r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP, r.aircraftType,         r.callsignColor, r.callsign + "|ATYP");
        cellTagClickable(FREQ, r.nextFreq, r.callsign + "|FREQ");
        cellTagClickable(HP,   r.hp,       r.callsign + "|HP",   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(QPOS, r.queuePos, r.callsign + "|QPOS", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(SPC,  r.spacing,  r.callsign + "|SPC",  DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellTag(TMR,  r.timeSinceTakeoff,                         DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellTag(LDST, r.liveSpacing,                              DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}

/// @brief Draws the TWR Inbound custom window from the pre-sorted twrInboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrInbound(HDC hDC)
{
    auto* settingsIn = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsIn->GetTwrInboundVisible()) { return; }
    int fo = settingsIn->GetFontOffset();
    int op = settingsIn->GetBgOpacity();

    const int TITLE_H      = 13;
    const int HDR_H        = 17 + fo;
    const int ROW_H        = 19 + fo;
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H        = 12;    ///< Height of blank separator row between runway groups
    const int PAD     = 6;
    // Column order: RWY, TTT, C/S, NM, SPD, WTC, ATYP, Gate, Vacate, ARW
    // Base widths at font size 17; scaled by (17+fo)/17:
    auto cw = [&](int base) -> int { return base * (17 + fo) / 17; };
    const int RWY     = cw(35);   //  front column — runway group label
    const int TTT     = cw(90);   //  "mm:ss" normally; "->nnn.nnn" (9 chars) on go-around
    const int CS      = cw(84);   // 12 chars
    const int NM      = cw(56);   //  8 chars
    const int GS      = cw(35);   //  5 chars
    const int WTC     = cw(28);   //  4 chars
    const int ATYP    = cw(56);   //  8 chars
    const int GATE    = cw(35);   //  5 chars
    const int VACATE  = cw(49);   //  7 chars
    const int ARW     = cw(49);   //  7 chars
    const int WIN_W   = PAD + RWY + TTT + CS + NM + GS + WTC + ATYP + GATE + VACATE + ARW + PAD;
    int numRows       = (int)this->twrInboundRowsCache.size();

    // Count separators and sum actual row heights
    int numSeps   = 0;
    int totalRowH = 0;
    {
        std::string lastGrp;
        for (const auto& r : this->twrInboundRowsCache)
        {
            if (!lastGrp.empty() && r.rwy != lastGrp) { ++numSeps; }
            lastGrp = r.rwy;
            totalRowH += r.dimmed ? DIMMED_ROW_H : ROW_H;
        }
    }

    const int WIN_H = TITLE_H + HDR_H + totalRowH + numSeps * SEP_H + PAD / 2;

    if (this->twrInboundWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->twrInboundWindowPos.x = 20;
        this->twrInboundWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->twrInboundWindowPos.x;
    int wy = this->twrInboundWindowPos.y;

    const int X_BTN = 11;
    RECT xRect = { wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN };
    this->winCloseLastHoverType = -1;
    HWND  hwndIn = WindowFromDC(hDC);
    POINT cursorIn;
    GetCursorPos(&cursorIn);
    if (hwndIn) { ScreenToClient(hwndIn, &cursorIn); }
    bool xHovered = PtInRect(&xRect, cursorIn) != 0;

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    FillRectAlpha(hDC, winRect,  RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect,  RGB(40, 40, 40), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Inbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRectIn = { wx, wy, wx + WIN_W - X_BTN - 2, wy + TITLE_H };
    AddScreenObject(SCREEN_OBJECT_TWR_IN_WIN, "TWRIN", dragRectIn, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "twrIn", xRect, false, "");

    // Column headers — smaller font
    HFONT hdrFont = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevDataFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int x = wx + PAD;
        int colY0 = wy + TITLE_H;
        auto colHdr = [&](int width, const char* label, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT r = { x, colY0, x + width, colY0 + HDR_H };
            DrawTextA(hDC, label, -1, &r, flags);
            x += width;
        };
        colHdr(RWY,     "RWY");
        colHdr(TTT,     "TTT");
        colHdr(CS,      "C/S");
        colHdr(NM,      "NM",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        colHdr(GS,      "GS",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        colHdr(WTC,     "W",      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP,    "ATYP");
        colHdr(GATE,    "Gate");
        colHdr(VACATE,  "Vacate");
        colHdr(ARW,     "ARW",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for the closest aircraft per runway, dimmed for the rest
    HFONT dataFont = CreateFontA(-17 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevDataFont = (HFONT)SelectObject(hDC, dataFont);
    auto wtcColor = [](char wtc) -> COLORREF {
        switch (wtc) {
            case 'L': return TAG_COLOR_TURQ;
            case 'H': return TAG_COLOR_ORANGE;
            case 'J': return TAG_COLOR_RED;
            default:  return TAG_COLOR_LIST_GRAY;
        }
    };

    std::string lastRwy;
    int rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->twrInboundRowsCache[row];
        int rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        // Insert a blank separator row between runway groups
        if (!lastRwy.empty() && r.rwy != lastRwy)
        {
            RECT sepRect = { wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H };
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }
        lastRwy = r.rwy;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        int rowY = rowTop;
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + rowH }; FillRectAlpha(hDC, alt, RGB(32, 32, 32), op); }
        int cx   = wx + PAD;

        // Plain clickable cell for non-tagInfo data (callsign, rwy group, wtc, groundspeed, gate).
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        // tagInfo-aware variants: honour bgColor, bold, and fontDelta in addition to text and colour.
        // When bgColor is set the fill is measured to the actual text extent and drawn as an overlay
        // on top of the normal row background, then text is redrawn on top of the fill.
        auto cellTag = [&](int width, const tagInfo& t, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont = CreateFontA(baseSize - delta, 0, 0, 0,
                                       t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int textLeft = (flags & DT_RIGHT)  ? rect.right - textSize.cx
                             : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                             :                       rect.left;
                int textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT fillRect = { (LONG)(textLeft - 3),          std::max(rect.top,    (LONG)(textTop - 1)),
                                  (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1)) };
                HBRUSH brush = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled) { SelectObject(hDC, r.dimmed ? dimFont : dataFont); DeleteObject(cellFont); }
            cx += width;
        };

        auto cellTagClickable = [&](int width, const tagInfo& t, const std::string& objId,
                                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont = CreateFontA(baseSize - delta, 0, 0, 0,
                                       t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int textLeft = (flags & DT_RIGHT)  ? rect.right - textSize.cx
                             : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                             :                       rect.left;
                int textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT fillRect = { (LONG)(textLeft - 3),          std::max(rect.top,    (LONG)(textTop - 1)),
                                  (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1)) };
                HBRUSH brush = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled) { SelectObject(hDC, r.dimmed ? dimFont : dataFont); DeleteObject(cellFont); }
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(RWY,     r.rwy,                  r.callsignColor, r.callsign + "|RWY");
        cellTagClickable(TTT,    r.ttt,    r.callsign + "|TTT");
        cellTagClickable(CS, { .tag     = r.callsign,
                                .color   = r.isGoAround ? TAG_COLOR_WHITE
                                         : r.isFrozen   ? TAG_COLOR_BLACK
                                         :                r.callsignColor,
                                .bgColor = r.isGoAround ? TAG_BG_COLOR_RED
                                         : r.isFrozen   ? TAG_BG_COLOR_YELLOW
                                         :                TAG_COLOR_DEFAULT_NONE },
                         r.callsign + "|CS");
        cellTagClickable(NM,     r.nm,     r.callsign + "|NM",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellClickable(GS,    std::format("{}", r.groundSpeed), TAG_COLOR_WHITE, r.callsign + "|GS",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(WTC, { .tag = std::string(1, r.wtc), .color = wtcColor(r.wtc), .bold = (r.wtc != 'M' && r.wtc != ' '), .fontDelta = (r.wtc != 'M' && r.wtc != ' ') ? 1 : 0 }, r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP,  r.aircraftType,                r.callsignColor, r.callsign + "|ATYP");
        cellTagClickable(GATE,   r.gate,  r.callsign + "|GATE");
        cellTagClickable(VACATE, r.vacate, r.callsign + "|VACATE");
        cellTagClickable(ARW,    r.arrRwy, r.callsign + "|ARW",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}

/// @brief Draws the NAP reminder as a custom floating window with a draggable body and an ACK button.
/// OnOverScreenObject calls RequestRefresh() on every mouse-movement event so this function runs
/// frequently. GetCursorPos() then reflects the live cursor position accurately at each draw.
void RadarScreen::DrawNapReminder(HDC hDC)
{
    if (!this->napReminderActive) { return; }

    const int TITLE_H = 20;
    const int MSG_H   = 40;
    const int BTN_H   = 28;
    const int PAD     = 10;
    const int WIN_W   = 360;
    const int WIN_H   = TITLE_H + MSG_H + BTN_H + PAD;

    if (this->napWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->napWindowPos.x = clip.right  / 2 - WIN_W / 2;
        this->napWindowPos.y = clip.bottom / 2 - WIN_H / 2;
    }

    int wx = this->napWindowPos.x;
    int wy = this->napWindowPos.y;

    RECT winRect  = { wx,       wy,                    wx + WIN_W, wy + WIN_H                    };
    RECT titleRect= { wx,       wy,                    wx + WIN_W, wy + TITLE_H                  };
    RECT bodyRect = { wx + PAD, wy + TITLE_H,          wx + WIN_W - PAD, wy + TITLE_H + MSG_H    };
    RECT btnRect  = { wx + PAD, wy + TITLE_H + MSG_H,  wx + WIN_W - PAD, wy + TITLE_H + MSG_H + BTN_H - 4 };
    // Drag zone covers title+body (does NOT overlap btnRect so ES fires separate OnOverScreenObject per area)
    RECT dragRect = { wx,       wy,                    wx + WIN_W, wy + TITLE_H + MSG_H          };

    // Reset hover tracking each frame so the next window entry always counts as a transition.
    this->napLastHoverType = -1;

    // Blink animation: 3 full cycles (6 phases × 80 ms) after click before the window closes.
    // Even phases show the pressed colour; odd phases show the hover colour.
    if (this->napAckClickTick != 0)
    {
        int phase = (int)((GetTickCount64() - this->napAckClickTick) / 80);
        if (phase >= 6)
        {
            // Animation finished — close window, stop sound, and persist dismissal
            this->napAckClickTick   = 0;
            this->napAckPressed     = false;
            this->napReminderActive = false;
            PlaySoundA(nullptr, nullptr, 0);
            static_cast<CFlowX_Timers*>(this->GetPlugIn())->AckNapReminder();
            return;
        }
        // Set pressed state for this phase and request the next frame immediately
        this->napAckPressed = (phase % 2 == 0);
        this->RequestRefresh();
    }

    // Hover: read live cursor position. RequestRefresh() from OnOverScreenObject (or the blink loop)
    // ensures this runs at event rate, so the cursor position is always current.
    HWND hwnd = WindowFromDC(hDC);
    POINT cursorPt;
    GetCursorPos(&cursorPt);
    if (hwnd) { ScreenToClient(hwnd, &cursorPt); }
    // During blink napAckPressed drives the colour; GetCursorPos hover is irrelevant then
    bool ackHovered = (this->napAckClickTick == 0) && PtInRect(&btnRect, cursorPt) != 0;

    // Background
    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &winRect,   bgBrush);
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(bgBrush);
    DeleteObject(titleBrush);

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title bar
    HFONT titleFont = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "Night SID Reminder", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_NAP_WIN, "NAPWIN", dragRect, true, "");

    // Message body
    HFONT msgFont = CreateFontA(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev = (HFONT)SelectObject(hDC, msgFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    std::string msg = "NAP procedure for " + this->napReminderAirport + "?";
    DrawTextA(hDC, msg.c_str(), -1, &bodyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(msgFont);

    // ACK button — three visual states: normal / hover / pressed
    COLORREF btnBg     = this->napAckPressed ? RGB( 90, 175,  90)
                       : ackHovered          ? RGB( 60, 130,  60)
                                             : RGB( 35,  90,  35);
    COLORREF btnBorder = this->napAckPressed ? RGB(200, 255, 200)
                       : ackHovered          ? RGB(120, 200, 120)
                                             : RGB( 80, 150,  80);
    auto btnBrush  = CreateSolidBrush(btnBg);
    FillRect(hDC, &btnRect, btnBrush);
    DeleteObject(btnBrush);

    auto btnBorderBrush = CreateSolidBrush(btnBorder);
    FrameRect(hDC, &btnRect, btnBorderBrush);
    DeleteObject(btnBorderBrush);

    HFONT btnFont = CreateFontA(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev = (HFONT)SelectObject(hDC, btnFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "ACK", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(btnFont);
    AddScreenObject(SCREEN_OBJECT_NAP_ACK, "NAPACK", btnRect, false, "Acknowledge NAP reminder");
}

/// @brief Draws the Start button pinned to the lower-right corner of the radar screen.
/// GetChatArea() is queried every frame: when the chat / flight-strip bay is open the button
/// floats above it; otherwise it sits above the raw clip-box bottom.
void RadarScreen::DrawStartButton(HDC hDC)
{
    const int BTN_W = 56;
    const int BTN_H = 20;

    // Re-read boundaries every frame so the button floats above whichever overlay is currently visible.
    // When chat is open, chat.top is the exact pixel where the panel begins — use it directly.
    // When chat is closed, clip.bottom (the GDI drawable boundary) is the true visual edge.
    RECT clip;
    GetClipBox(hDC, &clip);
    RECT chat     = GetChatArea();
    bool chatOpen = (chat.bottom > chat.top);
    int  bottom   = chatOpen ? chat.top : clip.bottom;
    int  bx       = clip.right - BTN_W;
    int  by       = bottom     - BTN_H;

    RECT btnRect = { bx, by, bx + BTN_W, by + BTN_H };

    // Reset hover tracking each frame so every new mouse-enter counts as a fresh transition.
    this->startBtnLastHoverType = -1;

    // Read live cursor position for hover detection; RequestRefresh() from OnOverScreenObject keeps this current.
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd) { ScreenToClient(hwnd, &cursor); }
    bool hovered = PtInRect(&btnRect, cursor) != 0;

    // Background — three visual states: normal / hover / pressed
    COLORREF bgColor     = this->startBtnPressed ? RGB( 70, 110, 170)
                         : hovered               ? RGB( 50,  85, 135)
                                                 : RGB( 30,  55,  95);
    COLORREF borderColor = this->startBtnPressed ? RGB(180, 210, 255)
                         : hovered               ? RGB(120, 160, 220)
                                                 : RGB( 80, 120, 180);

    auto bgBrush = CreateSolidBrush(bgColor);
    FillRect(hDC, &btnRect, bgBrush);
    DeleteObject(bgBrush);

    auto borderBrush = CreateSolidBrush(borderColor);
    FrameRect(hDC, &btnRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    HFONT font = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev = (HFONT)SelectObject(hDC, font);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "FlowX", -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(font);

    AddScreenObject(SCREEN_OBJECT_START_BTN, "STARTBTN", btnRect, false, "FlowX");
}

/// @brief Draws the popup menu above the Start button when startMenuOpen is true.
/// Uses the same clip-box and chat-area logic as DrawStartButton to stay flush above it.
/// Sections: Windows (window show/hide toggles), Commands (actions), Options (plugin toggles).
void RadarScreen::DrawStartMenu(HDC hDC)
{
    if (!this->startMenuOpen) { return; }

    const int BTN_H     = 20;  ///< Must match DrawStartButton::BTN_H (fixed)
    auto* base     = static_cast<CFlowX_Base*>(this->GetPlugIn());
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    const int fo        = settings->GetFontOffset();
    const int HEADER_H  = 16 + fo;  ///< Height of a section header row
    const int ITEM_H    = 20 + fo;  ///< Height of a clickable item row
    const int GAP_H     = 10;       ///< Spacer height inserted above each section header after the first
    const int OUTER_PAD = 6;        ///< Margin from the outer border to all content on every side
    const int CBX_S     = 9 + fo;   ///< Checkbox square size in pixels
    const int CBX_GAP   = 4;        ///< Gap between checkbox right edge and item label
    const int MENU_W    = 180 * (14 + fo) / 14; ///< Menu width scaled with item font; right-aligned with the button

    struct MenuRow { bool isHeader; const char* label; bool hasCheckbox; bool checked; int itemIdx; bool hasFontButtons = false; bool hasBgButtons = false; bool disabled = false; };

    const bool osmBusy = settings->IsOsmBusy();

    MenuRow rows[] = {
        { true,  "Windows",            false, false,                               -1 },
        { false, "Approach Estimate",  true,  settings->GetApproachEstVisible(),   16 },
        { false, "DEP/H",              true,  settings->GetDepRateVisible(),        4 },
        { false, "TWR Outbound",       true,  settings->GetTwrOutboundVisible(),    5 },
        { false, "TWR Inbound",        true,  settings->GetTwrInboundVisible(),     6 },
        { false, "WX/ATIS",            true,  settings->GetWeatherVisible(),        7 },
        { true,  "Commands",           false, false,                               -1 },
        { false, "Redo CLR flags",     false, false,                                0 },
        { false, "Dismiss QNH",        false, false,                               12 },
        { false, "Save positions",     false, false,                                1 },
        { true,  "Assists",            false, false,                               -1 },
        { false, "Auto-Restore FPLN",  true,  settings->GetAutoRestore(),           3 },
        { false, "Auto PARK",          true,  settings->GetAutoParked(),           15 },
        { false, "Auto-Clear Scratch", true,  settings->GetAutoScratchpadClear(),  18 },
        { true,  "Notifications",      false, false,                               -1 },
        { false, "Airborne",           true,  settings->GetSoundAirborne(),        19 },
        { false, "GND Transfer",       true,  settings->GetSoundGndTransfer(),     20 },
        { false, "Ready T/O",          true,  settings->GetSoundReadyTakeoff(),    21 },
        { true,  "Options",            false, false,                               -1 },
        { false, "Debug mode",         true,  base->GetDebug(),                     2 },
        { false, "Update check",       true,  settings->GetUpdateCheck(),          13 },
        { false, "Flash messages",     true,  settings->GetFlashOnMessage(),       14 },
        { false, "Appr Est Colors",    true,  settings->GetApprEstColors(),        17 },
        { false, "Fonts",              false, false,                               -1, true  },
        { false, "BG opacity",         false, false,                               -1, false, true },
        { true,  "TAXI",              false, false,                               -1 },
        { false, "Update TAXI info",  false, false,                               22, false, false, osmBusy },
        { false, "Show TAXI overlay", true,  this->showTaxiOverlay,               23 },
    };
    const int NUM_ROWS = (int)(sizeof(rows) / sizeof(rows[0]));

    // Compute total menu height: outer padding at bottom only (top header sits flush with the border), gaps before non-first headers, row heights.
    int MENU_H = OUTER_PAD;
    for (auto [i, row] : std::views::enumerate(rows))
        { MENU_H += (row.isHeader && i > 0) ? GAP_H + HEADER_H : row.isHeader ? HEADER_H : ITEM_H; }

    // Mirror DrawStartButton's anchor logic exactly.
    RECT clip;
    GetClipBox(hDC, &clip);
    RECT chat     = GetChatArea();
    bool chatOpen = (chat.bottom > chat.top);
    int  bottom   = chatOpen ? chat.top : clip.bottom;
    int  mx       = clip.right - MENU_W;
    int  my       = bottom - BTN_H - MENU_H;

    // Hover detection — reset each frame so every enter counts as a fresh transition.
    this->startMenuLastHoverType = -1;
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd) { ScreenToClient(hwnd, &cursor); }

    // Auto-close if the cursor moves more than 100 px away from the menu in any direction.
    const int CLOSE_DIST = 200;
    if (cursor.x < mx - CLOSE_DIST || cursor.x > mx + MENU_W + CLOSE_DIST ||
        cursor.y < my - CLOSE_DIST || cursor.y > my + MENU_H + CLOSE_DIST)
    {
        this->startMenuOpen = false;
        this->RequestRefresh();
        return;
    }

    // Overall background and border.
    RECT menuRect = { mx, my, mx + MENU_W, my + MENU_H };
    auto bgBrush  = CreateSolidBrush(RGB(15, 15, 15));
    FillRect(hDC, &menuRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(hDC, TRANSPARENT);

    HFONT headerFont = CreateFontA(-10 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT itemFont   = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT cbxFont    = CreateFontA(-11 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prev = (HFONT)SelectObject(hDC, itemFont);

    int iy = my;
    for (auto [i, row] : std::views::enumerate(rows))
    {
        int  rh      = row.isHeader ? HEADER_H : ITEM_H;
        RECT rowRect = { mx, iy, mx + MENU_W, iy + rh }; // rowRect for headers is updated after gap advance below

        if (row.isHeader)
        {
            // Gap spacer above every section header except the first.
            if (i > 0) { iy += GAP_H; }

            // Section header — blue title-bar background, inset by 1 so it doesn't paint over the border.
            rowRect = { mx + 1, iy, mx + MENU_W - 1, iy + HEADER_H };
            auto hdrBrush = CreateSolidBrush(RGB(30, 55, 95));
            FillRect(hDC, &rowRect, hdrBrush);
            DeleteObject(hdrBrush);

            SelectObject(hDC, headerFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            RECT textRect = { mx + OUTER_PAD, iy, mx + MENU_W - OUTER_PAD, iy + HEADER_H };
            DrawTextA(hDC, row.label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (row.hasFontButtons)
        {
            // Fonts row: label + current offset value on the left, [−] [+] buttons on the right.
            const int BTN_W = 14 + fo;
            int fo = settings->GetFontOffset();
            std::string valStr = (fo > 0) ? std::format("+{}", fo) : std::format("{}", fo);

            RECT minusRect = { mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                               mx + MENU_W - OUTER_PAD - BTN_W - 2,     iy + (ITEM_H + BTN_W) / 2 };
            RECT plusRect  = { mx + MENU_W - OUTER_PAD - BTN_W,         iy + (ITEM_H - BTN_W) / 2,
                               mx + MENU_W - OUTER_PAD,                  iy + (ITEM_H + BTN_W) / 2 };
            bool minusHov = PtInRect(&minusRect, cursor) != 0;
            bool plusHov  = PtInRect(&plusRect,  cursor) != 0;

            for (auto [rect, hov] : { std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov} })
            {
                auto btnBrush = CreateSolidBrush(hov ? RGB(45, 70, 115) : RGB(35, 35, 35));
                FillRect(hDC, &rect, btnBrush);
                DeleteObject(btnBrush);
                auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &rect, btnBorder);
                DeleteObject(btnBorder);
            }

            SelectObject(hDC, itemFont);
            std::string fullLabel = std::string("Fonts ") + valStr;
            RECT labelRect = { mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H };
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect,  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|8", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|9", plusRect,  false, "");
        }
        else if (row.hasBgButtons)
        {
            // BG opacity row: label + current value on the left, [−] [+] buttons on the right.
            const int BTN_W = 14 + fo;
            int bgOp = settings->GetBgOpacity();
            std::string valStr = std::format("{}%", bgOp);

            RECT minusRect = { mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                               mx + MENU_W - OUTER_PAD - BTN_W - 2,     iy + (ITEM_H + BTN_W) / 2 };
            RECT plusRect  = { mx + MENU_W - OUTER_PAD - BTN_W,         iy + (ITEM_H - BTN_W) / 2,
                               mx + MENU_W - OUTER_PAD,                  iy + (ITEM_H + BTN_W) / 2 };
            bool minusHov = PtInRect(&minusRect, cursor) != 0;
            bool plusHov  = PtInRect(&plusRect,  cursor) != 0;

            for (auto [rect, hov] : { std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov} })
            {
                auto btnBrush = CreateSolidBrush(hov ? RGB(45, 70, 115) : RGB(35, 35, 35));
                FillRect(hDC, &rect, btnBrush);
                DeleteObject(btnBrush);
                auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &rect, btnBorder);
                DeleteObject(btnBorder);
            }

            SelectObject(hDC, itemFont);
            std::string fullLabel = std::string("BG opacity ") + valStr;
            RECT labelRect = { mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H };
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect,  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|10", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|11", plusRect,  false, "");
        }
        else
        {
            // Clickable item row — hover highlight and optional checkbox.
            bool rowHovered = !row.disabled && PtInRect(&rowRect, cursor) != 0;
            if (rowHovered)
            {
                auto hoverBrush = CreateSolidBrush(RGB(45, 70, 115));
                FillRect(hDC, &rowRect, hoverBrush);
                DeleteObject(hoverBrush);
            }

            if (row.hasCheckbox)
            {
                int  cy      = iy + (ITEM_H - CBX_S) / 2;
                int  cx      = mx + OUTER_PAD;
                RECT boxRect = { cx, cy, cx + CBX_S, cy + CBX_S };

                auto boxBrush  = CreateSolidBrush(row.checked ? RGB(30, 55, 95) : RGB(25, 25, 25));
                FillRect(hDC, &boxRect, boxBrush);
                DeleteObject(boxBrush);

                auto boxBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &boxRect, boxBorder);
                DeleteObject(boxBorder);

                if (row.checked)
                {
                    SelectObject(hDC, cbxFont);
                    SetTextColor(hDC, TAG_COLOR_WHITE);
                    DrawTextA(hDC, "x", -1, &boxRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }

            SelectObject(hDC, itemFont);
            RECT textRect = { mx + OUTER_PAD + CBX_S + CBX_GAP, iy, mx + MENU_W - OUTER_PAD, iy + ITEM_H };
            SetTextColor(hDC, row.disabled  ? RGB(70, 70, 70)
                            : rowHovered    ? TAG_COLOR_WHITE
                                            : TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, row.label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (!row.disabled)
            {
                std::string objId = std::format("MENU|{}", row.itemIdx);
                AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, objId.c_str(), rowRect, false, row.label);
            }
        }

        iy += rh;
    }

    // Draw border last so it renders on top of all header/item fills.
    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &menuRect, borderBrush);
    DeleteObject(borderBrush);

    SelectObject(hDC, prev);
    DeleteObject(headerFont);
    DeleteObject(itemFont);
    DeleteObject(cbxFont);
}

/// @brief Draws the WX/ATIS window: wind, QNH, and ATIS letter in a single horizontal row.
/// Values are highlighted yellow when changed; clicking the row acknowledges them.
void RadarScreen::DrawWeatherWindow(HDC hDC)
{
    auto* settingsWx = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsWx->GetWeatherVisible()) { return; }
    int fo = settingsWx->GetFontOffset();
    int op = settingsWx->GetBgOpacity();

    const int TITLE_H = 13;
    const int DATA_H  = 36;
    const int RVR_H   = 26;   ///< Tighter row height for the RVR line
    const int WIN_PAD = 8;
    const int X_BTN   = 11;   ///< Width and height of the title-bar close button

    static const WeatherRowCache emptyRow{ "", "-", TAG_COLOR_DEFAULT_GRAY, "-", TAG_COLOR_DEFAULT_GRAY, "?", TAG_COLOR_DEFAULT_GRAY };
    const auto& r = this->weatherRowsCache.empty() ? emptyRow : this->weatherRowsCache[0];

    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFont  = CreateFontA(-20 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

    // Measure content widths with the data font before drawing any background
    HFONT prev = (HFONT)SelectObject(hDC, dataFont);
    SIZE spaceSize, qnhSize, atisSize, rvrSize;
    GetTextExtentPoint32A(hDC, " ",          1,                  &spaceSize);
    GetTextExtentPoint32A(hDC, r.qnh.c_str(),  (int)r.qnh.size(), &qnhSize);
    GetTextExtentPoint32A(hDC, r.atis.c_str(), (int)r.atis.size(), &atisSize);
    GetTextExtentPoint32A(hDC, r.rvr.c_str(),  (int)r.rvr.size(),  &rvrSize);

    // Wind is drawn in two parts (direction + speed) with a half-space gap between them
    const int   halfGap   = spaceSize.cx / 2;
    std::string windDir   = (r.wind.size() >= 3) ? r.wind.substr(0, 3) : r.wind;
    std::string windSpeed = (r.wind.size() >  3) ? r.wind.substr(3)    : "";
    SIZE windDirSize, windSpeedSize;
    GetTextExtentPoint32A(hDC, windDir.c_str(),   (int)windDir.size(),   &windDirSize);
    GetTextExtentPoint32A(hDC, windSpeed.c_str(), (int)windSpeed.size(), &windSpeedSize);
    int windTotalW = windDirSize.cx + halfGap + windSpeedSize.cx;

    bool hasRVR  = !r.rvr.empty();
    int line1W   = windTotalW + spaceSize.cx + qnhSize.cx + spaceSize.cx + atisSize.cx;
    int WIN_W    = WIN_PAD + (line1W > rvrSize.cx ? line1W : rvrSize.cx) + WIN_PAD;
    int WIN_H    = TITLE_H + DATA_H + (hasRVR ? RVR_H : 0) + WIN_PAD / 2;

    if (this->weatherWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->weatherWindowPos.x = clip.right - WIN_W - 20;
        this->weatherWindowPos.y = clip.top   + 20;
    }

    int wx = this->weatherWindowPos.x;
    int wy = this->weatherWindowPos.y;

    RECT xRect = { wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN };
    this->winCloseLastHoverType = -1;
    HWND  hwndWx = WindowFromDC(hDC);
    POINT cursorWx;
    GetCursorPos(&cursorWx);
    if (hwndWx) { ScreenToClient(hwndWx, &cursorWx); }
    bool xHovered = PtInRect(&xRect, cursorWx) != 0;

    RECT winRect   = { wx, wy, wx + WIN_W, wy + WIN_H   };
    RECT titleRect = { wx, wy, wx + WIN_W, wy + TITLE_H };

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "WX/ATIS", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT dragRectWx = { wx, wy, wx + WIN_W - X_BTN - 2, wy + TITLE_H };
    AddScreenObject(SCREEN_OBJECT_WEATHER_WIN, "WXATIS", dragRectWx, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "weather", xRect, false, "");

    SelectObject(hDC, dataFont);
    int textY = wy + TITLE_H + (DATA_H - spaceSize.cy) / 2;
    int x     = wx + WIN_PAD;

    auto drawField = [&](const std::string& text, COLORREF color, int w) {
        SetTextColor(hDC, color);
        TextOutA(hDC, x, textY, text.c_str(), (int)text.size());
        x += w + spaceSize.cx;
    };

    SetTextColor(hDC, r.windColor);
    TextOutA(hDC, x, textY, windDir.c_str(),   (int)windDir.size());
    TextOutA(hDC, x + windDirSize.cx + halfGap, textY, windSpeed.c_str(), (int)windSpeed.size());
    x += windTotalW + spaceSize.cx;
    drawField(r.qnh,  r.qnhColor,  qnhSize.cx);
    drawField(r.atis, r.atisColor, atisSize.cx);

    if (hasRVR)
    {
        textY += RVR_H;
        x = wx + WIN_PAD;
        SetTextColor(hDC, r.rvrColor);
        TextOutA(hDC, x, textY, r.rvr.c_str(), (int)r.rvr.size());
    }

    RECT clickRect = { wx, wy + TITLE_H, wx + WIN_W, wy + WIN_H };
    AddScreenObject(SCREEN_OBJECT_WEATHER_ROW, r.icao.c_str(), clickRect, false, "");

    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    DeleteObject(dataFont);
}

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
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnOverScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnOverScreenObject", "unknown exception"); }
}

/// @brief Sets the pressed state when the mouse button goes down over the ACK or Start button.
/// Also handles the GND transfer square, which must be triggered on button-down rather than click
/// because EuroScope's radar target selection logic may consume the full press+release sequence.
void RadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (this->debug) GetPlugIn()->DisplayUserMessage("FlowX", "GndXfr",
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
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnButtonDownScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnButtonDownScreenObject", "unknown exception"); }
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
    catch (const std::exception& e) { WriteExceptionToLog("OnButtonUpScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnButtonUpScreenObject", "unknown exception"); }
}

/// @brief Starts the ACK blink animation on click; the window closes after it completes.
/// Also handles Start button menu toggle, menu item selection, and window close buttons.
void RadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
    if (ObjectType == SCREEN_OBJECT_WIN_CLOSE && Button == EuroScopePlugIn::BUTTON_LEFT)
    {
        std::string id(sObjectId);
        auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
        if      (id == "approachEst") { settings->ToggleApproachEstVisible(); }
        else if (id == "depRate")    { settings->ToggleDepRateVisible(); }
        else if (id == "twrOut")     { settings->ToggleTwrOutboundVisible(); }
        else if (id == "twrIn")      { settings->ToggleTwrInboundVisible(); }
        else if (id == "weather")    { settings->ToggleWeatherVisible(); }
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
        auto sep = id.find('|');
        int idx  = -1;
        if (sep != std::string::npos)
        {
            idx = std::stoi(id.substr(sep + 1));
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
            else if (idx == 13) { settings->ToggleUpdateCheck(); }
            else if (idx == 14) { settings->ToggleFlashOnMessage(); }
            else if (idx == 15) { settings->ToggleAutoParked(); }
            else if (idx == 16) { settings->ToggleApproachEstVisible(); }
            else if (idx == 17) { settings->ToggleApprEstColors(); }
            else if (idx == 18) { settings->ToggleAutoScratchpadClear(); }
            else if (idx == 19) { settings->ToggleSoundAirborne(); }
            else if (idx == 20) { settings->ToggleSoundGndTransfer(); }
            else if (idx == 21) { settings->ToggleSoundReadyTakeoff(); }
            else if (idx == 22) // Update TAXI info
            {
                settings->StartOsmFetch();
            }
            else if (idx == 23) // Show TAXI overlay
            {
                this->showTaxiOverlay = !this->showTaxiOverlay;
            }
            else if (idx == 4) { settings->ToggleDepRateVisible(); }
            else if (idx == 5) { settings->ToggleTwrOutboundVisible(); }
            else if (idx == 6) { settings->ToggleTwrInboundVisible(); }
            else if (idx == 7) { settings->ToggleWeatherVisible(); }
            else if (idx == 8)  { settings->DecreaseFontOffset(); }
            else if (idx == 9)  { settings->IncreaseFontOffset(); }
            else if (idx == 10) { settings->DecreaseBgOpacity(); }
            else if (idx == 11) { settings->IncreaseBgOpacity(); }
        }
        // Keep menu open for window toggles (4-7, 16), notification toggles (19-21), and TAXI overlay toggle (23); close for all others
        if (idx < 4 || idx == 12 || idx == 22 || (idx >= 13 && idx != 16 && idx != 17 && idx < 19)) { this->startMenuOpen = false; }

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
        auto sep = id.rfind('|');
        if (sep == std::string::npos) { return; }
        std::string callsign = id.substr(0, sep);

        auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (aselFp.IsValid()) { GetPlugIn()->SetASELAircraft(aselFp); }

        if (Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            auto ctrl = this->GetPlugIn()->ControllerMyself();
            if (!ctrl.IsController() || ctrl.GetFacility() != 3) { return; }

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
        auto sep = id.find('|');
        if (sep == std::string::npos) { return; }
        std::string callsign = id.substr(0, sep);
        std::string col      = id.substr(sep + 1);

        // Select the aircraft as ASEL (mirrors what clicking any tag item does in the default lists)
        auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (aselFp.IsValid()) { GetPlugIn()->SetASELAircraft(aselFp); }

        // Locate the cached row so we can pass the current item string to StartTagFunction
        const TwrOutboundRowCache* rowPtr = nullptr;
        for (const auto& r : this->twrOutboundRowsCache)
        {
            if (r.callsign == callsign) { rowPtr = &r; break; }
        }
        if (!rowPtr) { return; }
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
        auto sep = id.find('|');
        if (sep == std::string::npos) { return; }
        std::string callsign = id.substr(0, sep);
        std::string col      = id.substr(sep + 1);

        // Select the aircraft as ASEL
        auto aselFp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (aselFp.IsValid()) { GetPlugIn()->SetASELAircraft(aselFp); }

        // Locate the cached row for item strings
        const TwrInboundRowCache* rowPtr = nullptr;
        for (const auto& r : this->twrInboundRowsCache)
        {
            if (r.callsign == callsign) { rowPtr = &r; break; }
        }
        if (!rowPtr) { return; }
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
    catch (const std::exception& e) { WriteExceptionToLog("OnClickScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnClickScreenObject", "unknown exception"); }
}

/// @brief Double-click handler: on the STS column, reverts LINEUP ground state back to TAXI.
void RadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    try
    {
        if (ObjectType != SCREEN_OBJECT_TWR_OUT_CELL || Button != EuroScopePlugIn::BUTTON_LEFT) { return; }

        std::string id(sObjectId);
        auto sep = id.find('|');
        if (sep == std::string::npos) { return; }
        std::string callsign = id.substr(0, sep);
        std::string col      = id.substr(sep + 1);

        if (col != "STS") { return; }

        auto fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
        if (!fp.IsValid()) { return; }

        // Only revert when the aircraft is currently in LINEUP state
        auto statusIt = std::ranges::find_if(this->twrOutboundRowsCache,
            [&callsign](const TwrOutboundRowCache& r) { return r.callsign == callsign; });
        if (statusIt == this->twrOutboundRowsCache.end()) { return; }
        if (statusIt->status.tag != "LINE UP") { return; }

        GetPlugIn()->SetASELAircraft(fp);
        this->StartTagFunction(callsign.c_str(),
            PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, statusIt->status.tag.c_str(),
            PLUGIN_NAME, TAG_FUNC_REVERT_TO_TAXI, Pt, Area);
        this->RequestRefresh();
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnDoubleClickScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnDoubleClickScreenObject", "unknown exception"); }
}

/// @brief Updates the screen-pixel anchor for a departure overlay when the radar target moves.
/// @param RadarTarget The target whose position has changed.
void RadarScreen::OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget)
{
    try
    {
        auto depInfoIt = this->radarTargetDepartureInfos.find(RadarTarget.GetCallsign());
        if (RadarTarget.IsValid() && depInfoIt != this->radarTargetDepartureInfos.end())
        {
            POINT screenPos = this->ConvertCoordFromPositionToPixel(RadarTarget.GetPosition().GetPosition());
            screenPos.x -= 16;
            screenPos.y += 3;
            depInfoIt->second.pos = screenPos;
        }
        if (RadarTarget.IsValid())
        {
            static_cast<CFlowX_CustomTags*>(this->GetPlugIn())->UpdatePositionDerivedTags(RadarTarget);
        }
    }
    catch (const std::exception& e) { WriteExceptionToLog("OnRadarTargetPositionUpdate", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnRadarTargetPositionUpdate", "unknown exception"); }
}

/// @brief Removes the departure overlay entry for a disconnecting flight plan.
/// @param FlightPlan The disconnecting flight plan.
void RadarScreen::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    try
    {
        auto findCallSign = this->radarTargetDepartureInfos.find(FlightPlan.GetCallsign());
        if (findCallSign != this->radarTargetDepartureInfos.end())
        {
            this->radarTargetDepartureInfos.erase(findCallSign);
        }
    }
    catch (const std::exception& e) { WriteExceptionToLog("RadarScreen::OnFlightPlanDisconnect", e.what()); }
    catch (...)                      { WriteExceptionToLog("RadarScreen::OnFlightPlanDisconnect", "unknown exception"); }
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
    auto dragWindow = [&](POINT& windowPos, POINT& lastDrag) {
        if (lastDrag.x == -1 || lastDrag.y == -1)
        {
            lastDrag = Pt;
        }
        windowPos.x += Pt.x - lastDrag.x;
        windowPos.y += Pt.y - lastDrag.y;
        lastDrag = Pt;
        if (Released)
        {
            lastDrag = { -1, -1 };
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
        if (this->approachEstResizeLastDrag.x == -1 || this->approachEstResizeLastDrag.y == -1)
        {
            this->approachEstResizeLastDrag = Pt;
        }
        this->approachEstWindowW = std::max(120, this->approachEstWindowW + (int)(Pt.x - this->approachEstResizeLastDrag.x));
        this->approachEstWindowH = std::max(200, this->approachEstWindowH + (int)(Pt.y - this->approachEstResizeLastDrag.y));
        this->approachEstResizeLastDrag = Pt;
        if (Released) { this->approachEstResizeLastDrag = { -1, -1 }; }
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
    catch (const std::exception& e) { WriteExceptionToLog("OnMoveScreenObject", e.what()); }
    catch (...)                      { WriteExceptionToLog("OnMoveScreenObject", "unknown exception"); }
}
