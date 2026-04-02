#include "pch.h"
#include "RadarScreen.h"
#include "CDelHelX_Base.h"
#include "CDelHelX_Tags.h"

#include <algorithm>
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
        this->DrawDepRateWindow(hDC);
        this->DrawTwrOutbound(hDC);
        this->DrawTwrInbound(hDC);
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
    const int COL_RWY = 28;
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

    auto bgBrush    = CreateSolidBrush(RGB(25, 25, 25));
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
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma");
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
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->depRateRowsCache[row];
        int rowY = wy + TITLE_H + HDR_H + row * ROW_H;

        RECT rwyRect = { wx + WIN_PAD,                                rowY, wx + WIN_PAD + COL_RWY,                   rowY + ROW_H };
        RECT cntRect = { wx + WIN_PAD + COL_RWY,                      rowY, wx + WIN_PAD + COL_RWY + COL_CNT,         rowY + ROW_H };
        RECT spcRect = { wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP, rowY, wx + WIN_W - WIN_PAD,                     rowY + ROW_H };

        SetTextColor(hDC, TAG_COLOR_DEFAULT_GRAY);
        DrawTextA(hDC, r.runway.c_str(),     -1, &rwyRect, DT_LEFT  | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.countColor);
        DrawTextA(hDC, r.countStr.c_str(),   -1, &cntRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.spacingColor);
        DrawTextA(hDC, r.spacingStr.c_str(), -1, &spcRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

/// @brief Draws the TWR Outbound custom window from the pre-sorted twrOutboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrOutbound(HDC hDC)
{
    const int TITLE_H = 13;
    const int HDR_H   = 17;
    const int ROW_H   = 15;
    const int PAD     = 6;
    // Original list widths in chars: C/S=12, STS=12, DEP?=10, RWY=4, SID=11, WTC=4, Freq=15, HP=4, Spacing=17
    const int CS      = 84;   // 12 chars
    const int WTC     = 28;   //  4 chars
    const int STS     = 84;   // 12 chars
    const int DEP     = 70;   // 10 chars
    const int RWY     = 28;   //  4 chars
    const int SID     = 77;   // 11 chars
    const int FREQ    = 105;  // 15 chars
    const int HP      = 28;   //  4 chars
    const int SPC     = 119;  // 17 chars
    const int WIN_W   = PAD + CS + WTC + STS + DEP + RWY + SID + FREQ + HP + SPC + PAD;
    int numRows       = (int)this->twrOutboundRowsCache.size();
    const int WIN_H   = TITLE_H + HDR_H + numRows * ROW_H + PAD / 2;

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

    auto bgBrush    = CreateSolidBrush(RGB(25, 25, 25));
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
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Outbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_TWR_OUT_WIN, "TWROUT", titleRect, true, "");

    // Column headers
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
        colHdr(WTC,  "W",    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(STS,  "STS");
        colHdr(DEP,  "DEP?");
        colHdr(RWY,  "RWY",  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SID,  "SID");
        colHdr(FREQ, "Freq");
        colHdr(HP,   "HP",   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(SPC,  "Spacing", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Data rows
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->twrOutboundRowsCache[row];
        int rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        int cx   = wx + PAD;

        auto cell = [&](int width, const std::string& text, COLORREF color,
                        UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + ROW_H };
            SetTextColor(hDC, color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            cx += width;
        };

        cell(CS,   r.callsign,            TAG_COLOR_WHITE);
        cell(WTC,  std::string(1, r.wtc), TAG_COLOR_DEFAULT_GRAY, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cell(STS,  r.status.tag,          r.status.color);
        cell(DEP,  r.depInfo.tag,         r.depInfo.color);
        cell(RWY,  r.rwy.tag,             r.rwy.color,             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cell(SID,  r.sameSid.tag,         r.sameSid.color);
        cell(FREQ, r.nextFreq.tag,        r.nextFreq.color);
        cell(HP,   r.hp.tag,              r.hp.color,              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cell(SPC,  r.spacing.tag,         r.spacing.color,         DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
    }
}

/// @brief Draws the TWR Inbound custom window from the pre-sorted twrInboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrInbound(HDC hDC)
{
    const int TITLE_H = 13;
    const int HDR_H   = 17;
    const int ROW_H   = 15;
    const int PAD     = 6;
    // Original list widths in chars: TTT=12, C/S=12, NM=8, SPD=5, WTC=4, RWY=7
    const int TTT     = 84;   // 12 chars
    const int CS      = 84;   // 12 chars
    const int NM      = 56;   //  8 chars
    const int GS      = 35;   //  5 chars
    const int WTC     = 28;   //  4 chars
    const int RWY     = 49;   //  7 chars
    const int WIN_W   = PAD + TTT + CS + NM + GS + WTC + RWY + PAD;
    int numRows       = (int)this->twrInboundRowsCache.size();
    const int WIN_H   = TITLE_H + HDR_H + numRows * ROW_H + PAD / 2;

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

    auto bgBrush    = CreateSolidBrush(RGB(25, 25, 25));
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
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma");
    HFONT prevFont = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Inbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_TWR_IN_WIN, "TWRIN", titleRect, true, "");

    // Column headers
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
        colHdr(TTT, "TTT");
        colHdr(CS,  "C/S");
        colHdr(NM,  "NM",  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(GS,  "GS",  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(WTC, "W",   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(RWY, "RWY", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Data rows
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r = this->twrInboundRowsCache[row];
        int rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        int cx   = wx + PAD;

        auto cell = [&](int width, const std::string& text, COLORREF color,
                        UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = { cx, rowY, cx + width, rowY + ROW_H };
            SetTextColor(hDC, color);
            DrawTextA(hDC, text.c_str(), -1, &rect, flags);
            cx += width;
        };

        cell(TTT, r.ttt.tag,                     r.ttt.color);
        cell(CS,  r.callsign,                     TAG_COLOR_WHITE);
        cell(NM,  r.nm.tag,                       r.nm.color,             DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cell(GS,  std::to_string(r.groundSpeed),  TAG_COLOR_DEFAULT_GRAY, DT_RIGHT  | DT_VCENTER | DT_SINGLELINE);
        cell(WTC, std::string(1, r.wtc),           TAG_COLOR_DEFAULT_GRAY, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cell(RWY, r.arrRwy.tag,                   r.arrRwy.color,         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
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
        static_cast<CDelHelX_Tags*>(this->GetPlugIn())->UpdatePositionDerivedTags(RadarTarget);
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
