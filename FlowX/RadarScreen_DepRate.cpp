/**
 * @file RadarScreen_DepRate.cpp
 * @brief RadarScreen partial implementation: DEP/H departure-rate window, departure info tag overlays, and popout creation.
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

/// @brief Draws per-aircraft departure info overlays (text, SID dot, HP label, connector line).
void RadarScreen::DrawDepartureInfoTag(HDC hDC)
{
    SetBkMode(hDC, TRANSPARENT);

    for (auto& [cs, info] : this->radarTargetDepartureInfos)
    {
        if (info.anchor.lat == 0.0 && info.anchor.lon == 0.0)
            continue;

        // Recompute pixel position from geo anchor every frame so panning/zooming moves the tag.
        EuroScopePlugIn::CPosition esPos;
        esPos.m_Latitude  = info.anchor.lat;
        esPos.m_Longitude = info.anchor.lon;
        POINT pos         = ConvertCoordFromPositionToPixel(esPos);
        pos.x -= 16;
        pos.y += 3;

        SetTextColor(hDC, info.dep_color);
        SIZE textSize;
        GetTextExtentPoint32A(hDC, info.dep_info.c_str(), (int)info.dep_info.length(), &textSize);
        TextOutA(hDC,
                 pos.x - textSize.cx + info.dragX,
                 pos.y + info.dragY,
                 info.dep_info.c_str(), (int)info.dep_info.length());

        RECT area;
        area.left   = pos.x - textSize.cx - 2 + info.dragX;
        area.top    = pos.y - 2 + info.dragY;
        area.right  = pos.x + 2 + info.dragX;
        area.bottom = pos.y + textSize.cy + 2 + info.dragY;

        auto sidBrush = CreateSolidBrush(info.sid_color);
        auto sidPen   = CreatePen(PS_SOLID, 1, info.sid_color);
        SelectObject(hDC, sidBrush);
        SelectObject(hDC, sidPen);
        RECT dotRect = {
            pos.x - textSize.cx + info.dragX + 2,
            pos.y + info.dragY + (area.bottom - area.top) - 5 + 2,
            pos.x - textSize.cx + info.dragX + 14,
            pos.y + info.dragY + (area.bottom - area.top) - 5 + 14};
        Ellipse(hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
        DeleteObject(sidBrush);
        DeleteObject(sidPen);

        if (!info.hp_info.empty())
        {
            SetTextColor(hDC, info.hp_color);
            TextOutA(hDC,
                     pos.x - textSize.cx + 18 + info.dragX,
                     pos.y + info.dragY + (area.bottom - area.top) - 5,
                     info.hp_info.c_str(), (int)info.hp_info.length());
            area.bottom += (area.bottom - area.top) - 5;
        }

        auto pen = CreatePen(PS_SOLID, 1, info.dep_color);
        SelectObject(hDC, pen);
        MoveToEx(hDC, pos.x + 16, pos.y - 3, nullptr);
        if (area.right <= pos.x + 16)
            LineTo(hDC, area.right, area.top + (area.bottom - area.top) / 2);
        else
            LineTo(hDC, area.left, area.top + (area.bottom - area.top) / 2);
        DeleteObject(pen);

        if (info.queue_pos > 0)
        {
            std::string seqStr      = std::to_string(info.queue_pos);
            HFONT       seqFont     = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
            HFONT       prevSeqFont = (HFONT)SelectObject(hDC, seqFont);
            SIZE        seqSize     = {};
            GetTextExtentPoint32A(hDC, seqStr.c_str(), (int)seqStr.length(), &seqSize);
            constexpr int PAD       = 3;
            constexpr int GAP       = 14; // distance from radar target dot to bottom of background box
            int           targetX   = pos.x + 16;
            int           targetY   = pos.y - 3;
            int           boxLeft   = targetX - seqSize.cx / 2 - PAD;
            int           boxTop    = targetY - GAP - seqSize.cy - PAD * 2;
            int           boxRight  = targetX + seqSize.cx / 2 + PAD + (seqSize.cx % 2 == 0 ? 0 : 1);
            int           boxBottom = targetY - GAP;
            RECT          bgRect    = {boxLeft, boxTop, boxRight, boxBottom};
            auto          bgBrush   = CreateSolidBrush(RGB(50, 50, 50));
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

/// @brief Draws the DEP/H departure-rate window from the pre-calculated depRateRowsCache.
void RadarScreen::DrawDepRateWindow(HDC hDC)
{
    auto* settingsDep = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsDep->GetDepRateVisible())
    {
        return;
    }
    int fo = settingsDep->GetFontOffset();
    int op = settingsDep->GetBgOpacity();

    const int TITLE_H = 13;
    const int HDR_H   = 17 + fo;
    const int ROW_H   = 15 + fo;
    const int WIN_PAD = 6;
    // Base column widths at font size 12; scaled by (12+fo)/12:
    auto cw = [&](int base) -> int
    { return base * (12 + fo) / 12; };
    const int COL_RWY = cw(35);
    const int COL_CNT = cw(20);
    const int COL_GAP = 6;
    const int COL_SPC = cw(56);
    const int WIN_W   = WIN_PAD + COL_RWY + COL_CNT + COL_GAP + COL_SPC + WIN_PAD;
    const int X_BTN   = 11; ///< Width and height of the title-bar close button
    int       numRows = (int)this->depRateRowsCache.size();
    const int WIN_H   = TITLE_H + HDR_H + numRows * ROW_H + WIN_PAD / 2;

    this->depRateWindowW = WIN_W;
    this->depRateWindowH = WIN_H;

    if (this->depRateWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->depRateWindowPos.x = clip.right - WIN_W - 20;
        this->depRateWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->depRateWindowPos.x;
    int wy = this->depRateWindowPos.y;

    // Close and popout button rects (inside title bar, inset 1 px from border)
    RECT xRect                   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT popRect                 = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType  = -1;
    this->winPopoutLastHoverType = -1;
    POINT cursorDep              = {-9999, -9999};
    if (this->isPopoutRender_)
        cursorDep = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorDep);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorDep);
    }
    bool xHovered   = PtInRect(&xRect, cursorDep) != 0;
    bool popHovered = PtInRect(&popRect, cursorDep) != 0;

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

    // Title row (smaller font, draggable — excludes close and popout button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    RECT dragRect = {wx, wy, popRect.left - 1, wy + TITLE_H};
    DrawTextA(hDC, "Departures", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DEPRATE_WIN, "DEPRATE", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "depRate", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "depRate", popRect, false, "");

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
        int  colY0  = wy + TITLE_H;
        int  splitX = wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP;
        RECT rwyHdr = {wx + WIN_PAD, colY0, wx + WIN_PAD + COL_RWY, colY0 + HDR_H};
        RECT cntHdr = {wx + WIN_PAD + COL_RWY, colY0, splitX, colY0 + HDR_H};
        RECT spcHdr = {splitX, colY0, wx + WIN_W - WIN_PAD, colY0 + HDR_H};
        DrawTextA(hDC, "RWY", -1, &rwyHdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "#", -1, &cntHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, "AVG", -1, &spcHdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // Data rows
    SelectObject(hDC, dataFontDep);
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r    = this->depRateRowsCache[row];
        int         rowY = wy + TITLE_H + HDR_H + row * ROW_H;
        if (row % 2 == 1)
        {
            RECT alt = {wx + 1, rowY, wx + WIN_W - 1, rowY + ROW_H};
            FillRectAlpha(hDC, alt, RGB(32, 32, 32), op);
        }

        RECT rwyRect = {wx + WIN_PAD, rowY, wx + WIN_PAD + COL_RWY, rowY + ROW_H};
        RECT cntRect = {wx + WIN_PAD + COL_RWY, rowY, wx + WIN_PAD + COL_RWY + COL_CNT, rowY + ROW_H};
        RECT spcRect = {wx + WIN_PAD + COL_RWY + COL_CNT + COL_GAP, rowY, wx + WIN_W - WIN_PAD, rowY + ROW_H};

        SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
        DrawTextA(hDC, r.runway.c_str(), -1, &rwyRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.countColor);
        DrawTextA(hDC, r.countStr.c_str(), -1, &cntRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hDC, r.spacingColor);
        DrawTextA(hDC, r.spacingStr.c_str(), -1, &spcRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFontDep);
    DeleteObject(hdrFontDep);
    DeleteObject(dataFontDep);
}

/// @brief Creates the Departure Rate popout window (fixed size — uses current window dimensions).
void RadarScreen::CreateDepRatePopout(CFlowX_Settings* s)
{
    int w = this->depRateWindowW;
    int h = this->depRateWindowH;
    int x = s->GetDepRatePopoutX();
    int y = s->GetDepRatePopoutY();
    if (x == -1)
    {
        x = this->depRateWindowPos.x;
        y = this->depRateWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetDepRatePopoutPos(x, y);
    }
    this->depRatePopout = std::make_unique<PopoutWindow>(
        "Departure Rate", x, y, w, h,
        [s](int nx, int ny)
        { s->SetDepRatePopoutPos(nx, ny); });
}
