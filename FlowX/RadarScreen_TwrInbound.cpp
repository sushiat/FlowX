/**
 * @file RadarScreen_TwrInbound.cpp
 * @brief RadarScreen partial implementation: TWR Inbound custom flight-plan list rendering.
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

/// @brief Draws the TWR Inbound custom window from the pre-sorted twrInboundRowsCache.
/// Column widths derived from the original EuroScope list definition char widths × 7px.
void RadarScreen::DrawTwrInbound(HDC hDC)
{
    auto* settingsIn = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsIn->GetTwrInboundVisible())
    {
        return;
    }
    int fo = settingsIn->GetFontOffset();
    int op = settingsIn->GetBgOpacity();

    const int TITLE_H      = 13;
    const int HDR_H        = 17 + fo;
    const int ROW_H        = 19 + fo;
    const int DIMMED_ROW_H = ROW_H - 3; ///< Reduced row height for dimmed rows (matches font size reduction 17→14)
    const int SEP_H        = 12;        ///< Height of blank separator row between runway groups
    const int PAD          = 6;
    // Column order: RWY, TTT, C/S, NM, SPD, WTC, ATYP, Gate, Vacate, ARW
    // Base widths at font size 17; scaled by (17+fo)/17:
    auto cw = [&](int base) -> int
    { return base * (17 + fo) / 17; };
    const int RWY     = cw(35); //  front column — runway group label
    const int TTT     = cw(90); //  "mm:ss" normally; "->nnn.nnn" (9 chars) on go-around
    const int CS      = cw(84); // 12 chars
    const int NM      = cw(56); //  8 chars
    const int GS      = cw(35); //  5 chars
    const int WTC     = cw(28); //  4 chars
    const int ATYP    = cw(56); //  8 chars
    const int GATE    = cw(35); //  5 chars
    const int VACATE  = cw(49); //  7 chars
    const int ARW     = cw(49); //  7 chars
    const int WIN_W   = PAD + RWY + TTT + CS + NM + GS + WTC + ATYP + GATE + VACATE + ARW + PAD;
    int       numRows = (int)this->twrInboundRowsCache.size();

    // Count separators and sum actual row heights
    int numSeps   = 0;
    int totalRowH = 0;
    {
        std::string lastGrp;
        for (const auto& r : this->twrInboundRowsCache)
        {
            if (!lastGrp.empty() && r.rwy != lastGrp)
            {
                ++numSeps;
            }
            lastGrp = r.rwy;
            totalRowH += r.dimmed ? DIMMED_ROW_H : ROW_H;
        }
    }

    const int WIN_H = TITLE_H + HDR_H + totalRowH + numSeps * SEP_H + PAD / 2;

    this->twrInboundWindowW = WIN_W;
    this->twrInboundWindowH = WIN_H;

    if (this->twrInboundWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->twrInboundWindowPos.x = 20;
        this->twrInboundWindowPos.y = clip.bottom - WIN_H - 20;
    }

    int wx = this->twrInboundWindowPos.x;
    int wy = this->twrInboundWindowPos.y;

    const int X_BTN             = 11;
    RECT      xRect             = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType = -1;
    POINT cursorIn              = {-9999, -9999};
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorIn);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorIn);
    }
    bool xHovered = PtInRect(&xRect, cursorIn) != 0;

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
    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title row (smaller font, draggable — excludes close button area)
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "TWR Inbound", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);
    RECT dragRectIn = {wx, wy, xRect.left - 1, wy + TITLE_H};
    AddScreenObject(SCREEN_OBJECT_TWR_IN_WIN, "TWRIN", dragRectIn, true, "");
    AddScreenObject(SCREEN_OBJECT_WIN_CLOSE, "twrIn", xRect, false, "");

    // Column headers — smaller font
    HFONT hdrFont      = CreateFontA(-12 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevDataFont = (HFONT)SelectObject(hDC, hdrFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    {
        int  x      = wx + PAD;
        int  colY0  = wy + TITLE_H;
        auto colHdr = [&](int width, const char* label, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT r = {x, colY0, x + width, colY0 + HDR_H};
            DrawTextA(hDC, label, -1, &r, flags);
            x += width;
        };
        colHdr(RWY, "RWY");
        colHdr(TTT, "TTT");
        colHdr(CS, "C/S");
        colHdr(NM, "NM", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(GS, "GS", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        colHdr(WTC, "W", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        colHdr(ATYP, "ATYP");
        colHdr(GATE, "Gate");
        colHdr(VACATE, "Vacate");
        colHdr(ARW, "ARW", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    prevDataFont   = (HFONT)SelectObject(hDC, dataFont);
    auto wtcColor  = [](char wtc) -> COLORREF
    {
        switch (wtc)
        {
        case 'L':
            return TAG_COLOR_TURQ;
        case 'H':
            return TAG_COLOR_ORANGE;
        case 'J':
            return TAG_COLOR_RED;
        default:
            return TAG_COLOR_LIST_GRAY;
        }
    };

    std::string lastRwy;
    int         rowTop = wy + TITLE_H + HDR_H;
    for (int row = 0; row < numRows; ++row)
    {
        const auto& r    = this->twrInboundRowsCache[row];
        int         rowH = r.dimmed ? DIMMED_ROW_H : ROW_H;

        // Insert a blank separator row between runway groups
        if (!lastRwy.empty() && r.rwy != lastRwy)
        {
            RECT sepRect = {wx + 1, rowTop, wx + WIN_W - 1, rowTop + SEP_H};
            FillRect(hDC, &sepRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            rowTop += SEP_H;
        }
        lastRwy = r.rwy;

        SelectObject(hDC, r.dimmed ? dimFont : dataFont);

        int rowY = rowTop;
        if (row % 2 == 1)
        {
            RECT alt = {wx + 1, rowY, wx + WIN_W - 1, rowY + rowH};
            FillRectAlpha(hDC, alt, RGB(32, 32, 32), op);
        }
        int cx = wx + PAD;

        // Plain clickable cell for non-tagInfo data (callsign, rwy group, wtc, groundspeed, gate).
        auto cellClickable = [&](int width, const std::string& text, COLORREF color,
                                 const std::string& objId, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};
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
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            cx += width;
        };

        auto cellTagClickable = [&](int width, const tagInfo& t, const std::string& objId,
                                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
        {
            RECT rect = {cx, rowY, cx + width, rowY + rowH};

            int   delta    = std::clamp(t.fontDelta, -4, 4);
            bool  styled   = t.bold || delta != 0;
            HFONT cellFont = nullptr;
            if (styled)
            {
                int baseSize = (r.dimmed ? -14 : -17) - fo;
                cellFont     = CreateFontA(baseSize - delta, 0, 0, 0,
                                           t.bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
                SelectObject(hDC, cellFont);
            }

            if (t.bgColor != TAG_COLOR_DEFAULT_NONE && !t.tag.empty())
            {
                SIZE textSize = {};
                GetTextExtentPoint32A(hDC, t.tag.c_str(), (int)t.tag.size(), &textSize);
                int    textLeft = (flags & DT_RIGHT)    ? rect.right - textSize.cx
                                  : (flags & DT_CENTER) ? rect.left + (width - textSize.cx) / 2
                                                        : rect.left;
                int    textTop  = rect.top + (rowH - textSize.cy) / 2;
                RECT   fillRect = {(LONG)(textLeft - 3), std::max(rect.top, (LONG)(textTop - 1)),
                                   (LONG)(textLeft + textSize.cx + 3), std::min(rect.bottom, (LONG)(textTop + textSize.cy + 1))};
                HBRUSH brush    = CreateSolidBrush(t.bgColor);
                FillRect(hDC, &fillRect, brush);
                DeleteObject(brush);
            }

            SetTextColor(hDC, t.color == TAG_COLOR_DEFAULT_GRAY ? TAG_COLOR_LIST_GRAY : t.color);
            DrawTextA(hDC, t.tag.c_str(), -1, &rect, flags);
            if (styled)
            {
                SelectObject(hDC, r.dimmed ? dimFont : dataFont);
                DeleteObject(cellFont);
            }
            AddScreenObject(SCREEN_OBJECT_TWR_IN_CELL, objId.c_str(), rect, false, "");
            cx += width;
        };

        cellClickable(RWY, r.rwy, r.callsignColor, r.callsign + "|RWY");
        cellTagClickable(TTT, r.ttt, r.callsign + "|TTT");
        cellTagClickable(CS, {.tag = r.callsign, .color = (r.isEmergency || r.isGoAround) ? TAG_COLOR_WHITE : r.isFrozen ? TAG_COLOR_BLACK
                                                                                                                         : r.callsignColor,
                              .bgColor = (r.isEmergency || r.isGoAround) ? TAG_BG_COLOR_RED : r.isFrozen ? TAG_BG_COLOR_YELLOW
                                                                                                         : TAG_COLOR_DEFAULT_NONE},
                         r.callsign + "|CS");
        cellTagClickable(NM, r.nm, r.callsign + "|NM", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellClickable(GS, std::format("{}", r.groundSpeed), TAG_COLOR_WHITE, r.callsign + "|GS", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        cellTagClickable(WTC, {.tag = std::string(1, r.wtc), .color = wtcColor(r.wtc), .bold = (r.wtc != 'M' && r.wtc != ' '), .fontDelta = (r.wtc != 'M' && r.wtc != ' ') ? 1 : 0}, r.callsign + "|WTC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        cellClickable(ATYP, r.aircraftType, r.callsignColor, r.callsign + "|ATYP");
        cellTagClickable(GATE, r.gate, r.callsign + "|GATE");
        cellTagClickable(VACATE, r.vacate, r.callsign + "|VACATE");
        cellTagClickable(ARW, r.arrRwy, r.callsign + "|ARW", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rowTop += rowH;
    }

    SelectObject(hDC, prevDataFont);
    DeleteObject(dataFont);
    DeleteObject(dimFont);
}
