/**
 * @file RadarScreen_ApproachEst.cpp
 * @brief RadarScreen partial implementation: Approach Estimate window rendering and popout creation.
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
#include <string>
#include "constants.h"
#include "helpers.h"

/// @brief Draws the Approach Estimate window: two vertical bars with a 300→0 s scale between them.
/// Left-side runways place their aircraft to the left of the left bar; right-side to the right of the right bar.
/// Runway labels appear at the top corners of the header row. Ticks every 5 s (short) and 10 s (long); labels every 20 s.
void RadarScreen::DrawApproachEstimateWindow(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settings->GetApproachEstVisible())
    {
        return;
    }

    const int fo         = settings->GetFontOffset();
    const int op         = settings->GetBgOpacity();
    const int TITLE_H    = 13;
    const int HDR_H      = 14 + fo; ///< Header row carrying the runway corner labels
    const int RESIZE_SZ  = 8;       ///< Size of the draggable resize square
    const int X_BTN      = 11;      ///< Close button size
    const int NUM_COL_W  = 48;      ///< Fixed width of the numbers column between the two bars
    const int TICK_LONG  = 10;      ///< Tick length (px into number column) for every-10-s marks
    const int TICK_SHORT = 8;       ///< Tick length for every-5-s (non-10-s) marks
    const int AC_PAD     = 4;       ///< Gap between the bar line and aircraft label text

    int WIN_W = this->approachEstWindowW;
    int WIN_H = this->approachEstWindowH;

    if (this->approachEstWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->approachEstWindowPos.x = clip.right - WIN_W - 20;
        this->approachEstWindowPos.y = (clip.top + clip.bottom) / 2 - WIN_H / 2;
    }

    int wx = this->approachEstWindowPos.x;
    int wy = this->approachEstWindowPos.y;

    // ── Geometry ──
    // Two bars flank a fixed-width numbers column, centred in the window.
    int barLx      = wx + (WIN_W - NUM_COL_W) / 2;
    int barRx      = barLx + NUM_COL_W;
    int numCenterX = barLx + NUM_COL_W / 2;

    int barTop    = wy + TITLE_H + HDR_H + 4;
    int barBottom = wy + WIN_H - RESIZE_SZ - 4;
    if (barBottom <= barTop)
    {
        barBottom = barTop + 1;
    }

    // Scale padding: inset the usable 300→0 range so aircraft text at the extremes stays readable.
    // Top pad: small margin above the 300-s marker line.
    // Bottom pad: enough for callsign + type below the 0-s marker line.
    const int SCALE_PAD_TOP    = 34;
    const int SCALE_PAD_BOTTOM = 34;
    int       scaleTop         = barTop + SCALE_PAD_TOP;
    int       scaleBottom      = barBottom - SCALE_PAD_BOTTOM;
    if (scaleBottom <= scaleTop)
    {
        scaleBottom = scaleTop + 1;
    }
    int barHeight = scaleBottom - scaleTop;

    // ── Close and popout button hover ──
    RECT  xRect   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT  popRect = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    POINT cursor  = {-9999, -9999};
    if (this->isPopoutRender_)
        cursor = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursor);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursor);
    }
    bool xHovered                = PtInRect(&xRect, cursor) != 0;
    bool popHovered              = PtInRect(&popRect, cursor) != 0;
    this->winPopoutLastHoverType = -1;

    // ── Background ──
    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT hdrRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + TITLE_H + HDR_H};

    FillRectAlpha(hDC, winRect, RGB(15, 15, 15), op);
    FillRectAlpha(hDC, hdrRect, RGB(40, 40, 40), op);

    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    if (xHovered)
    {
        auto xBrush = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &xRect, xBrush);
        DeleteObject(xBrush);
    }
    if (popHovered)
    {
        auto popBrush = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &popRect, popBrush);
        DeleteObject(popBrush);
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
    RECT dragRect = {wx, wy, popRect.left - 1, wy + TITLE_H};
    DrawTextA(hDC, "APPROACH ESTIMATE", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // "^" sits near the top of its cell at small font sizes; nudge rect down 1 px.
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);

    this->AddScreenObjectAuto(SCREEN_OBJECT_APPROACH_EST_WIN, "APPROACH_EST", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "approachEst", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "approachEst", popRect, false, "");

    // ── Runway corner labels — always from config, not from active inbounds ──
    auto [leftLabel, rightLabel] = settings->GetEstimateBarLabels();

    // ── Header row: runway labels at top corners ──
    HFONT hdrFont = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont      = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_DEFAULT_GRAY);
    if (!leftLabel.empty())
    {
        RECT lr = {wx + 3, wy + TITLE_H, wx + WIN_W / 2, wy + TITLE_H + HDR_H};
        DrawTextA(hDC, leftLabel.c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!rightLabel.empty())
    {
        RECT rr = {wx + WIN_W / 2, wy + TITLE_H, wx + WIN_W - 3, wy + TITLE_H + HDR_H};
        DrawTextA(hDC, rightLabel.c_str(), -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFont);
    DeleteObject(hdrFont);

    // ── Two vertical bars ──
    auto    barPen  = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    HGDIOBJ prevPen = SelectObject(hDC, barPen);
    MoveToEx(hDC, barLx, barTop, nullptr);
    LineTo(hDC, barLx, barBottom);
    MoveToEx(hDC, barRx, barTop, nullptr);
    LineTo(hDC, barRx, barBottom);
    SelectObject(hDC, prevPen);
    DeleteObject(barPen);

    // ── Tick marks and labels ──
    // Every 5 s: short tick inward from each bar; every 10 s: longer; labels every 20 s.
    HFONT tickFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prevFont       = (HFONT)SelectObject(hDC, tickFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    auto tickPen = CreatePen(PS_SOLID, 1, TAG_COLOR_WHITE);
    prevPen      = SelectObject(hDC, tickPen);

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
            SIZE        sz  = {};
            GetTextExtentPoint32A(hDC, lbl.c_str(), (int)lbl.size(), &sz);
            TextOutA(hDC, numCenterX - sz.cx / 2, ty - sz.cy / 2, lbl.c_str(), (int)lbl.size());
        }
    }

    SelectObject(hDC, prevPen);
    DeleteObject(tickPen);
    SelectObject(hDC, prevFont);
    DeleteObject(tickFont);

    // ── Aircraft labels ──
    HFONT csFont    = CreateFontA(-16 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT atFont    = CreateFontA(-13 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    bool  useColors = settings->GetApprEstColors();

    auto markerPen = CreatePen(PS_SOLID, 1, TAG_COLOR_GREEN);

    for (const auto& row : this->twrInboundRowsCache)
    {
        if (row.tttSeconds < 0 || row.tttSeconds > 300)
        {
            continue;
        }
        if (row.rwy.empty())
        {
            continue;
        }

        std::string side = settings->GetRunwayEstimateBarSide(row.rwy);
        if (side != "left" && side != "right")
        {
            continue;
        }

        int clampedTTT = std::clamp(row.tttSeconds, 0, 300);
        int y          = scaleTop + (300 - clampedTTT) * barHeight / 300;
        y              = std::clamp(y, scaleTop, scaleBottom);

        // Determine colors — either always-green or inbound-list colors
        COLORREF csColor  = TAG_COLOR_GREEN;
        COLORREF bgColor  = TAG_COLOR_DEFAULT_NONE;
        COLORREF textOnBg = TAG_COLOR_GREEN;
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
        prevFont    = (HFONT)SelectObject(hDC, csFont);
        SIZE csSize = {};
        GetTextExtentPoint32A(hDC, row.callsign.c_str(), (int)row.callsign.size(), &csSize);
        int csX = (side == "left") ? barLx - AC_PAD - csSize.cx : barRx + AC_PAD;
        int csY = y + 1;

        if (bgColor != TAG_COLOR_DEFAULT_NONE)
        {
            RECT   fillRect = {(LONG)(csX - 2), (LONG)(csY - 1), (LONG)(csX + csSize.cx + 2), (LONG)(csY + csSize.cy + 1)};
            HBRUSH bgBrush  = CreateSolidBrush(bgColor);
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
    RECT resizeRect  = {wx + WIN_W - RESIZE_SZ, wy + WIN_H - RESIZE_SZ, wx + WIN_W, wy + WIN_H};
    auto resizeBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hDC, &resizeRect, resizeBrush);
    DeleteObject(resizeBrush);
    this->AddScreenObjectAuto(SCREEN_OBJECT_APPROACH_EST_RESIZE, "APPROACH_EST_RESIZE", resizeRect, true, "");
}
/// @brief Creates the Approach Estimate popout window, seeding position/size from saved or current values.
void RadarScreen::CreateApproachEstPopout(CFlowX_Settings* s)
{
    int w = (s->GetApproachEstPopoutW() != -1) ? s->GetApproachEstPopoutW() : this->approachEstWindowW;
    int h = (s->GetApproachEstPopoutH() != -1) ? s->GetApproachEstPopoutH() : this->approachEstWindowH;
    int x = s->GetApproachEstPopoutX();
    int y = s->GetApproachEstPopoutY();
    if (x == -1)
    {
        x = this->approachEstWindowPos.x;
        y = this->approachEstWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetApproachEstPopoutPos(x, y);
        s->SetApproachEstPopoutSize(w, h);
    }
    this->approachEstPopout = std::make_unique<PopoutWindow>(
        "Approach Estimate", x, y, w, h,
        [s](int nx, int ny)
        { s->SetApproachEstPopoutPos(nx, ny); },
        nullptr,
        [](const HitArea& ha, POINT delta, int currentW, int currentH) -> std::pair<int, int>
        {
            if (ha.objectId != "APPROACH_EST_RESIZE")
                return {0, 0};
            return {std::max(120, currentW + (int)delta.x), std::max(200, currentH + (int)delta.y)};
        });
}
