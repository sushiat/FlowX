/**
 * @file RadarScreen.cpp
 * @brief Radar screen implementation; GDI rendering, drag interaction, and controller station tracking.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "RadarScreen.h"
#include "CDelHelX_Base.h"
#include "CDelHelX_CustomTags.h"
#include "CDelHelX_Timers.h"

#include <algorithm>
#include <mmsystem.h>
#include <string>
#include "constants.h"

RadarScreen::RadarScreen()
{
    this->debug = false;
}

RadarScreen::~RadarScreen() = default;

/// @brief Notifies the plugin that the screen is closing, then deletes this RadarScreen.
void RadarScreen::OnAsrContentToBeClosed()
{
    static_cast<CDelHelX_Base*>(GetPlugIn())->ClearRadarScreen();
    delete this;
}

/// @brief Adds the controller to the appropriate station set when they come online or update their position.
/// @param Controller The updated controller.
void RadarScreen::OnControllerPositionUpdate(EuroScopePlugIn::CController Controller)
{
    std::string cs = Controller.GetCallsign();
    std::transform(cs.begin(), cs.end(), cs.begin(), ::toupper);

    std::string myCS = this->GetPlugIn()->ControllerMyself().GetCallsign();
    std::transform(myCS.begin(), myCS.end(), myCS.begin(), ::toupper);

    // Not interested in observers, non-controllers and my own call-sign
    if (Controller.IsController() && Controller.GetRating() > 1 && cs != myCS)
    {
        double freq = Controller.GetPrimaryFrequency();
        auto freqString = std::to_string(freq);
        std::string freqFormatted = freqString.substr(0, freqString.find('.') + 4);

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

/// @brief Removes the controller from the appropriate station set when they go offline.
/// @param Controller The disconnected controller.
void RadarScreen::OnControllerDisconnect(EuroScopePlugIn::CController Controller)
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

/// @brief Dispatches all custom drawing to the four extract draw helpers; no calculations here.
/// @param hDC GDI device context for drawing.
/// @param Phase EuroScope refresh phase; all drawing occurs during REFRESH_PHASE_AFTER_TAGS.
void RadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS)
    {
        this->DrawDepartureInfoTag(hDC);
    }

    if (Phase == EuroScopePlugIn::REFRESH_PHASE_AFTER_LISTS)
    {
        this->DrawDepRateWindow(hDC);
        this->DrawTwrOutbound(hDC);
        this->DrawTwrInbound(hDC);
        this->DrawNapReminder(hDC);
        this->DrawWeatherWindow(hDC);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private draw helpers — pure GDI output, no state calculations
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Draws per-aircraft departure info overlays (text, SID dot, HP label, connector line).
void RadarScreen::DrawDepartureInfoTag(HDC hDC)
{
    SetBkMode(hDC, TRANSPARENT);

    for (auto it = this->radarTargetDepartureInfos.begin(); it != this->radarTargetDepartureInfos.end(); ++it)
    {
        if (it->second.pos.x <= -1 || it->second.pos.y <= -1) { continue; }

        SetTextColor(hDC, it->second.dep_color);
        SIZE textSize;
        GetTextExtentPoint32A(hDC, it->second.dep_info.c_str(), (int)it->second.dep_info.length(), &textSize);
        TextOutA(hDC,
                 it->second.pos.x - textSize.cx + it->second.dragX,
                 it->second.pos.y + it->second.dragY,
                 it->second.dep_info.c_str(), (int)it->second.dep_info.length());

        RECT area;
        area.left   = it->second.pos.x - textSize.cx - 2 + it->second.dragX;
        area.top    = it->second.pos.y - 2 + it->second.dragY;
        area.right  = it->second.pos.x + 2 + it->second.dragX;
        area.bottom = it->second.pos.y + textSize.cy + 2 + it->second.dragY;

        auto sidBrush = CreateSolidBrush(it->second.sid_color);
        auto sidPen   = CreatePen(PS_SOLID, 1, it->second.sid_color);
        SelectObject(hDC, sidBrush);
        SelectObject(hDC, sidPen);
        RECT dotRect = {
            it->second.pos.x - textSize.cx + it->second.dragX + 2,
            it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5 + 2,
            it->second.pos.x - textSize.cx + it->second.dragX + 14,
            it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5 + 14 };
        Ellipse(hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
        DeleteObject(sidBrush);
        DeleteObject(sidPen);

        if (!it->second.hp_info.empty())
        {
            SetTextColor(hDC, it->second.hp_color);
            TextOutA(hDC,
                     it->second.pos.x - textSize.cx + 18 + it->second.dragX,
                     it->second.pos.y + it->second.dragY + (area.bottom - area.top) - 5,
                     it->second.hp_info.c_str(), (int)it->second.hp_info.length());
            area.bottom += (area.bottom - area.top) - 5;
        }

        auto pen = CreatePen(PS_SOLID, 1, it->second.dep_color);
        SelectObject(hDC, pen);
        MoveToEx(hDC, it->second.pos.x + 16, it->second.pos.y - 3, nullptr);
        if (area.right <= it->second.pos.x + 16)
            LineTo(hDC, area.right, area.top + (area.bottom - area.top) / 2);
        else
            LineTo(hDC, area.left,  area.top + (area.bottom - area.top) / 2);
        DeleteObject(pen);

        AddScreenObject(SCREEN_OBJECT_DEP_TAG, it->first.c_str(), area, true, "");
    }
}

/// @brief Draws the DEP/H departure-rate window from the pre-calculated depRateRowsCache.
void RadarScreen::DrawDepRateWindow(HDC hDC)
{
    const int TITLE_H = 13;
    const int HDR_H   = 17;
    const int ROW_H   = 15;
    const int WIN_PAD = 6;
    const int COL_RWY = 35;
    const int COL_CNT = 20;
    const int COL_GAP = 6;
    const int COL_SPC = 56;
    const int WIN_W   = WIN_PAD + COL_RWY + COL_CNT + COL_GAP + COL_SPC + WIN_PAD;
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

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    auto hdrBrush   = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hDC, &winRect,   bgBrush);
    FillRect(hDC, &titleRect, titleBrush);
    FillRect(hDC, &hdrRect,   hdrBrush);
    DeleteObject(bgBrush);
    DeleteObject(titleBrush);
    DeleteObject(hdrBrush);

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "Departures", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_DEPRATE_WIN, "DEPRATE", titleRect, true, "");

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
    auto altBrushDep = CreateSolidBrush(RGB(32, 32, 32));
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->depRateRowsCache[row];
        int rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + ROW_H }; FillRect(hDC, &alt, altBrushDep); }

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
    DeleteObject(altBrushDep);
}

/// @brief Draws the TWR Outbound custom window from the pre-sorted twrOutboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrOutbound(HDC hDC)
{
    const int TITLE_H = 13;
    const int HDR_H   = 17;
    const int ROW_H   = 19;
    const int PAD     = 6;
    // Original list column order and widths in chars:
    // C/S=12, STS=12, DEP?=10, RWY=4, SID=11, WTC=4, ATYP=8, Freq=15, HP=4, Spacing=17
    const int CS      = 84;   // 12 chars
    const int STS     = 84;   // 12 chars
    const int DEP     = 70;   // 10 chars
    const int RWY     = 35;   //  4 chars (widened: "RWY" label needs more than 4×7px)
    const int SID     = 77;   // 11 chars
    const int WTC     = 28;   //  4 chars
    const int ATYP    = 56;   //  8 chars
    const int FREQ    = 105;  // 15 chars
    const int HP      = 28;   //  4 chars
    const int SPC     = 119;  // 17 chars
    const int TMR     = 42;   //  6 chars ("9:59")
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H   = 12;    ///< Height of blank separator row between sort groups
    const int WIN_W   = PAD + CS + STS + DEP + RWY + SID + WTC + ATYP + FREQ + HP + SPC + TMR + PAD;
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

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    auto hdrBrush   = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hDC, &winRect,   bgBrush);
    FillRect(hDC, &titleRect, titleBrush);
    FillRect(hDC, &hdrRect,   hdrBrush);
    DeleteObject(bgBrush);
    DeleteObject(titleBrush);
    DeleteObject(hdrBrush);

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Outbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_TWR_OUT_WIN, "TWROUT", titleRect, true, "");

    // Column headers — smaller font
    HFONT hdrFont = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        colHdr(SPC,  "Spacing", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(TMR,  "T+",      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for active aircraft, dimmed for departed+untracked
    HFONT dataFont = CreateFontA(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    auto altBrushOut = CreateSolidBrush(RGB(32, 32, 32));
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
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + rowH }; FillRect(hDC, &alt, altBrushOut); }
        int cx   = wx + PAD;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        auto cell = [&](int width, const std::string& text, COLORREF color,
                        UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            cx += width;
        };

        // Like cell(), but also registers a screen object so the cell is left/right-clickable.
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_OUT_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(CS,   r.callsign,             r.callsignColor, r.callsign + "|CS");
        cellClickable(STS,  r.status.tag,           r.status.color,  r.callsign + "|STS");
        cellClickable(DEP,  r.depInfo.tag,          r.depInfo.color, r.callsign + "|DEP");
        cellClickable(RWY,  r.rwy.tag,              r.rwy.color,     r.callsign + "|RWY", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(SID,  r.sameSid.tag,          r.sameSid.color, r.callsign + "|SID");
        cellClickable(WTC,  std::string(1, r.wtc),  wtcColor(r.wtc), r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP, r.aircraftType,         r.callsignColor, r.callsign + "|ATYP");
        cellClickable(FREQ, r.nextFreq.tag,         r.nextFreq.color, r.callsign + "|FREQ");
        cellClickable(HP,   r.hp.tag,               r.hp.color,       r.callsign + "|HP",  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(SPC,  r.spacing.tag,          r.spacing.color,  r.callsign + "|SPC", DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cell(TMR,  r.timeSinceTakeoff.tag,  r.timeSinceTakeoff.color,                      DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }
    DeleteObject(altBrushOut);

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}

/// @brief Draws the TWR Inbound custom window from the pre-sorted twrInboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrInbound(HDC hDC)
{
    const int TITLE_H = 13;
    const int HDR_H   = 17;
    const int ROW_H        = 19;
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H        = 12;    ///< Height of blank separator row between runway groups
    const int PAD     = 6;
    // Column order: RWY_GRP (new), TTT, C/S, NM, SPD, WTC, ATYP, Gate, Vacate, RWY
    const int RWY_GRP = 35;   //  new front column — runway group label
    const int TTT     = 56;   //  now displays "mm:ss" only (~5 chars)
    const int CS      = 84;   // 12 chars
    const int NM      = 56;   //  8 chars
    const int GS      = 35;   //  5 chars
    const int WTC     = 28;   //  4 chars
    const int ATYP    = 56;   //  8 chars
    const int GATE    = 35;   //  5 chars
    const int VACATE  = 49;   //  7 chars
    const int RWY     = 49;   //  7 chars
    const int WIN_W   = PAD + RWY_GRP + TTT + CS + NM + GS + WTC + ATYP + GATE + VACATE + RWY + PAD;
    int numRows       = (int)this->twrInboundRowsCache.size();

    // Count separators and sum actual row heights
    int numSeps   = 0;
    int totalRowH = 0;
    {
        std::string lastGrp;
        for (const auto& r : this->twrInboundRowsCache)
        {
            if (!lastGrp.empty() && r.rwyGroup != lastGrp) { ++numSeps; }
            lastGrp = r.rwyGroup;
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

    RECT winRect   = { wx, wy,            wx + WIN_W, wy + WIN_H            };
    RECT titleRect = { wx, wy,            wx + WIN_W, wy + TITLE_H          };
    RECT hdrRect   = { wx, wy + TITLE_H,  wx + WIN_W, wy + TITLE_H + HDR_H  };

    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    auto hdrBrush   = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hDC, &winRect,   bgBrush);
    FillRect(hDC, &titleRect, titleBrush);
    FillRect(hDC, &hdrRect,   hdrBrush);
    DeleteObject(bgBrush);
    DeleteObject(titleBrush);
    DeleteObject(hdrBrush);

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Inbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_TWR_IN_WIN, "TWRIN", titleRect, true, "");

    // Column headers — smaller font
    HFONT hdrFont = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        colHdr(RWY_GRP, "RWY");
        colHdr(TTT,     "TTT");
        colHdr(CS,      "C/S");
        colHdr(NM,      "NM",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        colHdr(GS,      "GS",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        colHdr(WTC,     "W",      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP,    "ATYP");
        colHdr(GATE,    "Gate");
        colHdr(VACATE,  "Vacate");
        colHdr(RWY,     "ARW",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevDataFont);
    DeleteObject(hdrFont);

    // Data rows — normal font for the closest aircraft per runway, dimmed for the rest
    HFONT dataFont = CreateFontA(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dimFont  = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    auto altBrushIn = CreateSolidBrush(RGB(32, 32, 32));
    std::string lastRwyGroup;
    int rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->twrInboundRowsCache[row];
        int rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        // Insert a blank separator row between runway groups
        if (!lastRwyGroup.empty() && r.rwyGroup != lastRwyGroup)
        {
            RECT sepRect = { wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H };
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }
        lastRwyGroup = r.rwyGroup;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        int rowY = rowTop;
        if (row % 2 == 1) { RECT alt = { wx + 1, rowY, wx + WIN_W - 1, rowY + rowH }; FillRect(hDC, &alt, altBrushIn); }
        int cx   = wx + PAD;

        auto cell = [&](int width, const std::string& text, COLORREF color,
                        UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            cx += width;
        };

        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + rowH };
            SetTextColor(hDC, color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(RWY_GRP, r.rwyGroup,                  r.callsignColor, r.callsign + "|RWYGRP");
        cellClickable(TTT,   r.ttt.tag,                     r.ttt.color,    r.callsign + "|TTT");
        cellClickable(CS,    r.callsign,                    r.callsignColor, r.callsign + "|CS");
        cellClickable(NM,    r.nm.tag,                      r.nm.color,      r.callsign + "|NM",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellClickable(GS,    std::to_string(r.groundSpeed), TAG_COLOR_WHITE, r.callsign + "|GS",     DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cellClickable(WTC,   std::string(1, r.wtc),         wtcColor(r.wtc), r.callsign + "|WTC",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP,  r.aircraftType,                r.callsignColor, r.callsign + "|ATYP");
        cellClickable(GATE,  r.gate,                        TAG_COLOR_WHITE, r.callsign + "|GATE");
        cellClickable(VACATE,r.vacate.tag,                  r.vacate.color,  r.callsign + "|VACATE");
        cellClickable(RWY,   r.arrRwy.tag,                  r.arrRwy.color,  r.callsign + "|RWY",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }
    DeleteObject(altBrushIn);

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
            static_cast<CDelHelX_Timers*>(this->GetPlugIn())->AckNapReminder();
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

/// @brief Draws the WX/ATIS window: wind, QNH, and ATIS letter in a single horizontal row.
/// Values are highlighted yellow when changed; clicking the row acknowledges them.
void RadarScreen::DrawWeatherWindow(HDC hDC)
{
    const int TITLE_H = 13;
    const int DATA_H  = 36;
    const int RVR_H   = 26;   ///< Tighter row height for the RVR line
    const int WIN_PAD = 8;

    static const WeatherRowCache emptyRow{ "", "-", TAG_COLOR_DEFAULT_GRAY, "-", TAG_COLOR_DEFAULT_GRAY, "?", TAG_COLOR_DEFAULT_GRAY };
    const auto& r = this->weatherRowsCache.empty() ? emptyRow : this->weatherRowsCache[0];

    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFont  = CreateFontA(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    RECT winRect   = { wx, wy, wx + WIN_W, wy + WIN_H   };
    RECT titleRect = { wx, wy, wx + WIN_W, wy + TITLE_H };

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

    SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "WX/ATIS", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    AddScreenObject(SCREEN_OBJECT_WEATHER_WIN, "WXATIS", titleRect, true, "");

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
    if (ObjectType == SCREEN_OBJECT_NAP_WIN || ObjectType == SCREEN_OBJECT_NAP_ACK)
    {
        if (ObjectType != this->napLastHoverType)
        {
            this->napLastHoverType = ObjectType;
            this->RequestRefresh();
        }
    }
}

/// @brief Sets the pressed state when the mouse button goes down over the ACK button.
void RadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SCREEN_OBJECT_NAP_ACK)
    {
        this->napAckPressed = true;
        this->RequestRefresh();
    }
}

/// @brief Clears the pressed state when the mouse button is released.
void RadarScreen::OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SCREEN_OBJECT_NAP_ACK)
    {
        this->napAckPressed = false;
        this->RequestRefresh();
    }
}

/// @brief Starts the ACK blink animation on click; the window closes after it completes.
void RadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SCREEN_OBJECT_NAP_ACK && this->napAckClickTick == 0)
    {
        this->napAckClickTick = GetTickCount64();
        this->RequestRefresh();
    }

    if (ObjectType == SCREEN_OBJECT_WEATHER_ROW)
    {
        static_cast<CDelHelX_Timers*>(this->GetPlugIn())->AckWeather(std::string(sObjectId));
        this->RequestRefresh();
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

        if ((col == "TTT" || col == "RWYGRP") && Button == EuroScopePlugIn::BUTTON_LEFT)
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
                    GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.c_str(),
                    GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_FUNC_STAND_MENU, Pt, Area);
            }
            else
            {
                this->StartTagFunction(callsign.c_str(),
                    GROUNDRADAR_PLUGIN_NAME, GROUNDRADAR_TAG_TYPE_ASSIGNED_STAND, r.gate.c_str(),
                    PLUGIN_NAME, TAG_FUNC_STAND_AUTO, Pt, Area);
            }
        }
        else if (col == "RWY" && Button == EuroScopePlugIn::BUTTON_LEFT)
        {
            this->StartTagFunction(callsign.c_str(),
                PLUGIN_NAME, TAG_ITEM_ASSIGNED_ARR_RUNWAY, r.arrRwy.tag.c_str(),
                nullptr, EuroScopePlugIn::TAG_ITEM_FUNCTION_TAKE_HANDOFF, Pt, Area);
        }

        this->RequestRefresh();
    }
}

/// @brief Double-click handler: on the STS column, reverts LINEUP ground state back to TAXI.
void RadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
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
    auto statusIt = std::find_if(this->twrOutboundRowsCache.begin(), this->twrOutboundRowsCache.end(),
        [&callsign](const TwrOutboundRowCache& r) { return r.callsign == callsign; });
    if (statusIt == this->twrOutboundRowsCache.end()) { return; }
    if (statusIt->status.tag != "LINE UP") { return; }

    GetPlugIn()->SetASELAircraft(fp);
    this->StartTagFunction(callsign.c_str(),
        PLUGIN_NAME, TAG_ITEM_GND_STATE_EXPANDED, statusIt->status.tag.c_str(),
        PLUGIN_NAME, TAG_FUNC_REVERT_TO_TAXI, Pt, Area);
    this->RequestRefresh();
}

/// @brief Updates the screen-pixel anchor for a departure overlay when the radar target moves.
/// @param RadarTarget The target whose position has changed.
void RadarScreen::OnRadarTargetPositionUpdate(EuroScopePlugIn::CRadarTarget RadarTarget)
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
        static_cast<CDelHelX_CustomTags*>(this->GetPlugIn())->UpdatePositionDerivedTags(RadarTarget);
    }
}

/// @brief Removes the departure overlay entry for a disconnecting flight plan.
/// @param FlightPlan The disconnecting flight plan.
void RadarScreen::OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan)
{
    auto findCallSign = this->radarTargetDepartureInfos.find(FlightPlan.GetCallsign());
    if (findCallSign != this->radarTargetDepartureInfos.end())
    {
        this->radarTargetDepartureInfos.erase(findCallSign);
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
