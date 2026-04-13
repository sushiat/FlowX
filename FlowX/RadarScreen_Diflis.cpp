/**
 * @file RadarScreen_Diflis.cpp
 * @brief RadarScreen partial implementation: DIFLIS digital flight strip window rendering and popout creation.
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
#include "DiflisModel.h"

/// @brief Draws the DIFLIS (Digital Flight Strip) window — slice 1: window frame, column layout,
/// group headers, one strip variant (simplified), and a status bar with clock + ATIS + UNDO.
/// Strip drag/drop and group-transition writeback are added in a later slice.
void RadarScreen::DrawDiflisWindow(HDC hDC)
{
    auto* settingsD = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsD->GetDiflisVisible())
    {
        return;
    }

    const int TITLE_H = 13;
    const int X_BTN   = 11;
    const int PAD     = 4;

    int WIN_W = this->diflisWindowW;
    int WIN_H = this->diflisWindowH;

    this->diflisWindowW = WIN_W;
    this->diflisWindowH = WIN_H;

    if (this->diflisWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->diflisWindowPos.x = std::max<LONG>(20, (clip.right - WIN_W) / 2);
        this->diflisWindowPos.y = std::max<LONG>(40, (clip.bottom - WIN_H) / 2);
    }

    int wx = this->diflisWindowPos.x;
    int wy = this->diflisWindowPos.y;

    // Font scale tracks window height, roughly 0.45..1.0 between ~640 and ~1440
    float scale = std::clamp(WIN_H / 1440.0f, 0.45f, 1.0f);
    auto  fs    = [scale](int base) -> int
    { return -std::max(7, (int)std::round(base * scale)); };
    const int STATUS_H = std::max(28, (int)std::round(36 * scale));

    // ── Title bar + close/popout buttons ─────────────────────────────────────
    RECT xRect   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT popRect = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};

    // Maximize + topmost toggles; only visible when this is a popout render (the two
    // buttons live 4px to the left of the popin button, separated by the gap).
    constexpr int GAP      = 4;
    RECT          topRect  = {0, 0, 0, 0};
    RECT          maxRect  = {0, 0, 0, 0};
    if (this->isPopoutRender_)
    {
        topRect = {popRect.left - GAP - X_BTN - 1, wy + 1, popRect.left - GAP - 1, wy + 1 + X_BTN};
        maxRect = {topRect.left - X_BTN - 1, wy + 1, topRect.left - 1, wy + 1 + X_BTN};
    }

    POINT cursorD = {-9999, -9999};
    if (this->isPopoutRender_)
        cursorD = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorD);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorD);
    }
    bool xHovered   = PtInRect(&xRect, cursorD) != 0;
    bool popHovered = PtInRect(&popRect, cursorD) != 0;
    bool topHovered = this->isPopoutRender_ && PtInRect(&topRect, cursorD) != 0;
    bool maxHovered = this->isPopoutRender_ && PtInRect(&maxRect, cursorD) != 0;
    bool isTopmost   = !this->diflisPopout || this->diflisPopout->IsTopmost();
    bool isMaximized = this->diflisPopout && this->diflisPopout->IsMaximized();

    RECT winRect    = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect  = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT bodyRect   = {wx, wy + TITLE_H, wx + WIN_W, wy + WIN_H - STATUS_H};
    RECT statusRect = {wx, wy + WIN_H - STATUS_H, wx + WIN_W, wy + WIN_H};

    auto bodyBrush = CreateSolidBrush(RGB(28, 28, 28));
    FillRect(hDC, &winRect, bodyBrush);
    DeleteObject(bodyBrush);

    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &titleRect, titleBrush);
    DeleteObject(titleBrush);

    auto statusBrush = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hDC, &statusRect, statusBrush);
    DeleteObject(statusBrush);

    RECT statusSep    = {wx, statusRect.top - 3, wx + WIN_W, statusRect.top};
    auto statusSepBr  = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hDC, &statusSep, statusSepBr);
    DeleteObject(statusSepBr);

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
    if (topHovered)
    {
        auto b = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &topRect, b);
        DeleteObject(b);
    }
    if (maxHovered)
    {
        auto b = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &maxRect, b);
        DeleteObject(b);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    // Title text
    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    RECT dragRect = {wx, wy,
                     (this->isPopoutRender_ ? maxRect.left : popRect.left) - 1, wy + TITLE_H};
    DrawTextA(hDC, "DIFLIS", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (this->isPopoutRender_)
    {
        DrawTextA(hDC, isMaximized ? "R" : "M", -1, &maxRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextA(hDC, isTopmost ? "T" : "_", -1, &topRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);

    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_WIN, "DIFLIS", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "diflis", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "diflis", popRect, false, "");
    if (this->isPopoutRender_)
    {
        this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_MAXIMIZE_BTN, "diflis", maxRect, false, "");
        this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_TOPMOST_BTN, "diflis", topRect, false, "");
    }

    // ── Resolve airport diflis config ────────────────────────────────────────
    auto                  apIt = settingsD->FindMyAirport();
    const DiflisAirportConfig* dcfg = nullptr;
    std::string           myIcao;
    if (apIt != settingsD->GetAirports().end())
    {
        dcfg   = &apIt->second.diflis;
        myIcao = apIt->first;
    }

    // ── Column layout ────────────────────────────────────────────────────────
    const int NUM_COLS   = 4;
    const int bodyX0     = bodyRect.left + PAD;
    const int bodyY0     = bodyRect.top + PAD;
    const int bodyX1     = bodyRect.right - PAD;
    const int bodyY1     = bodyRect.bottom - PAD;
    const int bodyW      = std::max(1, bodyX1 - bodyX0);
    const int bodyH      = std::max(1, bodyY1 - bodyY0);
    const int COL_GAP    = 8;

    // Per-column widths from config; fall back to equal share if missing/misconfigured.
    int widths[NUM_COLS] = {25, 25, 25, 25};
    if (dcfg && (int)dcfg->columnWidths.size() >= NUM_COLS)
    {
        for (int i = 0; i < NUM_COLS; ++i)
            widths[i] = std::max(1, dcfg->columnWidths[i]);
    }
    int totalPct = widths[0] + widths[1] + widths[2] + widths[3];
    if (totalPct <= 0)
        totalPct = 1;
    int innerW = std::max(1, bodyW - (NUM_COLS - 1) * COL_GAP);
    int colX[NUM_COLS + 1];
    colX[0]        = bodyX0;
    int accumulated = 0;
    for (int i = 0; i < NUM_COLS - 1; ++i)
    {
        accumulated += (innerW * widths[i]) / totalPct;
        colX[i + 1] = bodyX0 + accumulated + (i + 1) * COL_GAP;
    }
    colX[NUM_COLS] = bodyX1;

    // Paint the inter-column gaps black across the full body (title→status bar).
    {
        auto gapBr = CreateSolidBrush(RGB(0, 0, 0));
        for (int i = 0; i < NUM_COLS - 1; ++i)
        {
            RECT gapR = {colX[i + 1] - COL_GAP, bodyRect.top, colX[i + 1], bodyRect.bottom};
            FillRect(hDC, &gapR, gapBr);
        }
        DeleteObject(gapBr);
    }

    auto mkFont = [](int h, int weight) -> HFONT
    {
        return CreateFontA(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    };
    const int fsStatus  = dcfg ? dcfg->fontSizeStatusBar   : 20;
    const int fsHdr     = dcfg ? dcfg->fontSizeGroupHeader : 16;
    const int fsHdrSide = dcfg ? dcfg->fontSizeGroupSide   : 14;
    const int fsLarge   = dcfg ? dcfg->fontSizeStripLarge  : 30;
    const int fsMedium  = dcfg ? dcfg->fontSizeStripMedium : 20;
    const int fsSmall   = dcfg ? dcfg->fontSizeStripSmall  : 16;

    HFONT groupHdrFont   = mkFont(fs(fsHdr), FW_BOLD);
    HFONT groupSideFont  = mkFont(fs(fsHdrSide), FW_BOLD);

    // Strip fonts scale with strip width. Baseline: a strip that fills 28% of a 1440p
    // window (i.e. column[0] at the reference 28% width). Wider strips scale up, narrower down.
    const float STRIP_WIDTH_BASELINE = 1440.0f * 0.34f;

    // Natural row heights per variant. Scales with both the global window scale and
    // the per-column strip width scale (narrower columns → smaller fonts → shorter strips).
    auto variantH = [&](DiflisStripVariant v, float wScale) -> int
    {
        float s = scale * wScale;
        switch (v)
        {
        case DiflisStripVariant::Collapsed: return std::max(20, (int)std::round(50 * s));
        case DiflisStripVariant::Expanded:  return std::max(40, (int)std::round(110 * s));
        }
        return 22;
    };
    auto isExpandedVariant = [](DiflisStripVariant v)
    { return v == DiflisStripVariant::Expanded; };

    // ── Per-column layout: active groups, weights, rects ─────────────────────
    if (dcfg)
    {
        for (int c = 0; c < NUM_COLS; ++c)
        {
            struct Entry
            {
                const DiflisGroupDef* def;
                int                  stripCount;
            };
            std::vector<Entry> entries;
            float              totalWeight = 0.0f;

            for (const auto& g : dcfg->groups)
            {
                if (g.columnIndex != c)
                    continue;
                int cnt = 0;
                for (const auto& s : this->diflisStripsCache)
                    if (s.resolvedGroupId == g.id)
                        ++cnt;
                if (g.collapseWhenEmpty && cnt == 0)
                    continue;
                entries.push_back({&g, cnt});
                totalWeight += std::max(0.01f, g.heightWeight);
            }
            if (entries.empty() || totalWeight <= 0.0f)
                continue;

            int cx0 = colX[c];
            int cx1 = colX[c + 1] - (c < NUM_COLS - 1 ? COL_GAP : 0);
            int cy  = bodyY0;
            int colH = bodyH;

            // Per-column strip fonts, sized from the column's actual strip width.
            float widthScale  = std::clamp(float(cx1 - cx0 - 8) / STRIP_WIDTH_BASELINE, 0.35f, 2.0f);
            HFONT csLargeFont   = mkFont(fs((int)std::round(fsLarge  * widthScale)), FW_BOLD);
            HFONT bodyLargeFont = mkFont(fs((int)std::round(fsMedium * widthScale)), FW_NORMAL);
            HFONT stripSmallFont= mkFont(fs((int)std::round(fsSmall  * widthScale)), FW_NORMAL);
            (void)stripSmallFont;

            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& e = entries[i];
                float w = std::max(0.01f, e.def->heightWeight);
                int   gh = (i + 1 == entries.size()) ? (bodyY0 + colH - cy)
                                                     : (int)std::round(colH * (w / totalWeight));
                if (gh < 18)
                    gh = 18;

                RECT groupRect = {cx0, cy, cx1, cy + gh};
                // Group body background
                auto gBrush = CreateSolidBrush(RGB(40, 40, 40));
                FillRect(hDC, &groupRect, gBrush);
                DeleteObject(gBrush);
                auto gBorder = CreateSolidBrush(RGB(70, 70, 70));
                FrameRect(hDC, &groupRect, gBorder);
                DeleteObject(gBorder);

                // Group header bar
                const int HDR_H = std::max(14, (int)std::round(16 * scale));
                RECT hdrRect    = {cx0, cy, cx1, cy + HDR_H};
                auto hdrBrush   = CreateSolidBrush(RGB(15, 15, 15));
                FillRect(hDC, &hdrRect, hdrBrush);
                DeleteObject(hdrBrush);

                SelectObject(hDC, groupHdrFont);
                SetTextColor(hDC, TAG_COLOR_WHITE);
                // Spaced-out title: "ARRIVALS" -> "A R R I V A L S"
                std::string spaced;
                spaced.reserve(e.def->title.size() * 2);
                for (size_t k = 0; k < e.def->title.size(); ++k)
                {
                    if (k > 0)
                        spaced.push_back(' ');
                    spaced.push_back(e.def->title[k]);
                }
                RECT titleTR = hdrRect;
                DrawTextA(hDC, spaced.c_str(), -1, &titleTR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (!e.def->rightHeaderText.empty())
                {
                    SelectObject(hDC, groupSideFont);
                    RECT rhTR = {cx0, cy, cx1 - 4, cy + HDR_H};
                    SetTextColor(hDC, RGB(170, 170, 170));
                    DrawTextA(hDC, e.def->rightHeaderText.c_str(), -1, &rhTR,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }

                // Strips — pick a variant that fits
                int stripAreaTop = cy + HDR_H + 1;
                int stripAreaBot = cy + gh - 1;
                int avail        = std::max(0, stripAreaBot - stripAreaTop);
                if (e.stripCount > 0 && avail > 0)
                {
                    DiflisStripVariant chosen  = e.def->preferredVariant;
                    int               rowH    = variantH(chosen, widthScale);
                    int               visible = e.stripCount;

                    if (rowH * visible > avail)
                    {
                        // Preferred doesn't fit — try fallback
                        DiflisStripVariant fb   = e.def->fallbackVariant;
                        int               fbH  = variantH(fb, widthScale);
                        if (fbH * visible <= avail)
                        {
                            chosen = fb;
                            rowH   = fbH;
                        }
                        else
                        {
                            // Still doesn't fit — use the smaller variant and truncate with "+N more"
                            chosen  = fb;
                            rowH    = fbH;
                            visible = std::max(0, avail / std::max(1, rowH) - 1);
                        }
                    }

                    const bool expandedV = isExpandedVariant(chosen);
                    HFONT      csFont    = csLargeFont;
                    HFONT      bodyFont  = bodyLargeFont;

                    int drawn = 0;
                    int sy    = e.def->stackBottom ? (stripAreaBot - rowH)
                                                   : stripAreaTop;
                    for (const auto& s : this->diflisStripsCache)
                    {
                        if (s.resolvedGroupId != e.def->id)
                            continue;
                        if (drawn >= visible)
                            break;
                        if (e.def->stackBottom ? (sy < stripAreaTop)
                                               : (sy + rowH > stripAreaBot))
                            break;

                        RECT stripR = {cx0 + 4, sy, cx1 - 4, sy + rowH - 3};

                        COLORREF bg     = s.bg;
                        COLORREF bgDark = s.bgDark;
                        auto sBrush = CreateSolidBrush(bg);
                        FillRect(hDC, &stripR, sBrush);
                        DeleteObject(sBrush);

                        // 2px border kept as plain background; all content inset inside it.
                        RECT inner = {stripR.left + 2, stripR.top + 2, stripR.right - 2, stripR.bottom - 2};

                        // Callsign cell width sized to fit "WZZ2929" in the large font. In the expanded
                        // variant the same horizontal block also hosts type+stand in its bottom third,
                        // so widen it if needed to fit "TYPE STND" underneath the callsign.
                        SelectObject(hDC, csFont);
                        SIZE szCs = {};
                        GetTextExtentPoint32A(hDC, "WZZ2929", 7, &szCs);
                        int  csW = szCs.cx + 8;
                        if (expandedV)
                        {
                            SelectObject(hDC, bodyFont);
                            SIZE szBot = {};
                            GetTextExtentPoint32A(hDC, "WW380 WZW", 9, &szBot);
                            csW = std::max(csW, (int)(szBot.cx + 12));
                            SelectObject(hDC, csFont);
                        }
                        RECT csR = {inner.left, inner.top, inner.left + csW, inner.bottom};
                        // Dark fill: full height in collapsed, only top 2/3 in expanded (bottom row
                        // = type + stand on the strip's normal bg).
                        RECT csDarkR = csR;
                        if (expandedV)
                            csDarkR.bottom = csR.top + 2 * (csR.bottom - csR.top) / 3;
                        auto csBr = CreateSolidBrush(bgDark);
                        FillRect(hDC, &csDarkR, csBr);
                        DeleteObject(csBr);

                        SelectObject(hDC, csFont);
                        SetTextColor(hDC, s.text);
                        std::string csLabel = s.callsign;
                        switch (s.wtc)
                        {
                        case 'H': case 'h': csLabel += "+"; break;
                        case 'J': case 'j': csLabel += "^"; break;
                        case 'L': case 'l': csLabel += "-"; break;
                        default: break;
                        }
                        {
                            RECT csTxt = {csDarkR.left + 6, csDarkR.top, csDarkR.right - 4, csDarkR.bottom};
                            int  avail = csTxt.right - csTxt.left;
                            SIZE szLbl = {};
                            GetTextExtentPoint32A(hDC, csLabel.c_str(), (int)csLabel.size(), &szLbl);
                            HFONT csShrunk = nullptr;
                            if (szLbl.cx > avail && avail > 0)
                            {
                                float ratio = (float)avail / (float)szLbl.cx;
                                int   baseH = std::abs(fs((int)std::round(fsLarge * widthScale)));
                                int   newH  = -std::max(6, (int)std::round(baseH * ratio));
                                csShrunk = mkFont(newH, FW_BOLD);
                                SelectObject(hDC, csShrunk);
                            }
                            DrawTextA(hDC, csLabel.c_str(), -1, &csTxt,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                            if (csShrunk)
                            {
                                SelectObject(hDC, csFont);
                                DeleteObject(csShrunk);
                            }
                        }

                        // Status button on the right — square in the expanded variant (= expanded inner height),
                        // same fixed width in collapsed so strips line up vertically. Label uses the
                        // callsign font for a consistent visual weight.
                        SelectObject(hDC, csFont);
                        int  stW  = std::max(1, variantH(DiflisStripVariant::Expanded, widthScale) - 3 - 4);
                        RECT stBtnR = {inner.right - stW, inner.top, inner.right, inner.bottom};
                        auto stBr   = CreateSolidBrush(bgDark);
                        FillRect(hDC, &stBtnR, stBr);
                        DeleteObject(stBr);
                        SetTextColor(hDC, s.text);
                        DrawTextA(hDC, "STAT", -1, &stBtnR,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                        // Body text — layout depends on variant
                        const int bx0 = csR.right + 4;
                        const int bx1 = stBtnR.left - 4;
                        const int by0 = inner.top + 1;
                        const int by1 = inner.bottom - 1;

                        std::string type   = s.acType;
                        std::string stand  = s.stand;
                        std::string sqk    = s.squawk;
                        std::string adx    = s.originOrDest;
                        std::string route  = s.sidOrStar;
                        std::string rwy    = s.rwy;
                        std::string status = s.status;

                        if (chosen == DiflisStripVariant::Collapsed)
                        {
                            // Collapsed: 5 cols total (callsign | type | stand | rwy | status).
                            // Callsign + status already drawn as outer dark cells. Body splits
                            // into 3 cells: type (medium), stand (medium), rwy (large).
                            // Type + stand are compact on the left (content-sized in medium
                            // font). Rwy fills the remaining width with text right-aligned
                            // against the status button.
                            const int SEP_W = 2;
                            SelectObject(hDC, bodyFont);
                            SIZE szType = {};
                            GetTextExtentPoint32A(hDC, "WW380W", 6, &szType);
                            int typeW = szType.cx + 10;
                            SIZE szStand = {};
                            GetTextExtentPoint32A(hDC, "A12", 3, &szStand);
                            int standW = szStand.cx + 10;

                            RECT typeR  = {bx0, by0, bx0 + typeW, by1};
                            RECT standR = {typeR.right + SEP_W, by0, typeR.right + SEP_W + standW, by1};
                            RECT rwyR   = {standR.right + SEP_W, by0, bx1, by1};

                            // Separators (2px darker), only between non-dark neighbours.
                            auto sepBr = CreateSolidBrush(bgDark);
                            RECT sep1  = {typeR.right, by0, typeR.right + SEP_W, by1};
                            RECT sep2  = {standR.right, by0, standR.right + SEP_W, by1};
                            FillRect(hDC, &sep1, sepBr);
                            FillRect(hDC, &sep2, sepBr);
                            DeleteObject(sepBr);

                            SetTextColor(hDC, s.text);
                            SelectObject(hDC, bodyFont);
                            RECT tTxt = {typeR.left + 4, typeR.top, typeR.right - 4, typeR.bottom};
                            DrawTextA(hDC, type.c_str(), -1, &tTxt,
                                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            RECT sTxt = {standR.left + 4, standR.top, standR.right - 4, standR.bottom};
                            DrawTextA(hDC, stand.c_str(), -1, &sTxt,
                                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            // Rwy column holds SID (left, medium) + rwy (right, large), no
                            // separator between them. SID is shortened: first 3 letters of
                            // the alpha prefix + numeric/suffix tail (e.g. LANUX3D -> LAN3D).
                            std::string sidShort;
                            if (!s.isInbound)
                            {
                                sidShort = route;
                                size_t digit = 0;
                                while (digit < sidShort.size() && !std::isdigit((unsigned char)sidShort[digit]))
                                    ++digit;
                                if (digit > 3)
                                    sidShort = sidShort.substr(0, 3) + sidShort.substr(digit);
                            }
                            SelectObject(hDC, bodyFont);
                            SetTextColor(hDC, s.textDim);
                            RECT sidTxt = {rwyR.left + 4, rwyR.top, rwyR.right - 6, rwyR.bottom};
                            DrawTextA(hDC, sidShort.c_str(), -1, &sidTxt,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            SelectObject(hDC, csFont);
                            SetTextColor(hDC, s.textDim);
                            RECT rTxt = {rwyR.left + 4, rwyR.top, rwyR.right - 6, rwyR.bottom};
                            DrawTextA(hDC, rwy.c_str(), -1, &rTxt,
                                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        }
                        else  // Expanded
                        {
                            // Expanded layout (6 columns, 3 rows):
                            //   col1+col2: callsign (rowspan 2, colspan 2, dark bg) above;
                            //             row3 = type (col1) + stand (col2) on normal bg
                            //   col3: r1 SID, r2 "443" placeholder, r3 origin/dest
                            //   col4: r1 empty, r2 squawk, r3 empty
                            //   col5: flex; r3 runway right-aligned
                            //   col6: status button (already drawn)
                            const int hBody = inner.bottom - inner.top;
                            const int r2top = inner.top + hBody / 3;
                            const int r3top = inner.top + 2 * hBody / 3;

                            // Row 3 under the callsign block: type (left half) + stand (right half)
                            int  csBotTop = r3top;
                            int  half     = csR.left + (csR.right - csR.left) / 2;
                            RECT typeBotR = {csR.left + 6, csBotTop, half - 2, inner.bottom};
                            RECT standBotR = {half + 2, csBotTop, csR.right - 4, inner.bottom};
                            SelectObject(hDC, bodyFont);
                            SetTextColor(hDC, s.text);
                            DrawTextA(hDC, s.acType.c_str(), -1, &typeBotR,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            DrawTextA(hDC, s.stand.c_str(), -1, &standBotR,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                            // SID shortening for departures (LANUX3D -> LAN3D)
                            std::string sidShort;
                            if (!s.isInbound)
                            {
                                sidShort = route;
                                size_t digit = 0;
                                while (digit < sidShort.size() &&
                                       !std::isdigit((unsigned char)sidShort[digit]))
                                    ++digit;
                                if (digit > 3)
                                    sidShort = sidShort.substr(0, 3) + sidShort.substr(digit);
                            }

                            // Column widths (content-sized via body font)
                            SelectObject(hDC, bodyFont);
                            SIZE szSid = {}, szSqk = {};
                            GetTextExtentPoint32A(hDC, "WWWWW", 5, &szSid);
                            GetTextExtentPoint32A(hDC, "9999",  4, &szSqk);
                            const int SEP_W = 4;
                            int col3W = szSid.cx + 10;
                            int col4W = szSqk.cx + 10;
                            int col3x = csR.right + SEP_W;
                            int col4x = col3x + col3W + SEP_W;
                            int col5x = col4x + col4W + SEP_W;
                            int col5r = stBtnR.left - SEP_W;

                            // Inter-column separators — same rule as collapsed: 2 px darker bars,
                            // only drawn where both sides are bright cells (never alongside the
                            // dark callsign block or the dark status button).
                            {
                                auto sepBr = CreateSolidBrush(bgDark);
                                // sep type|stand under the callsign block (bottom third only);
                                // leave a 2 px gap below the dark callsign cell above.
                                RECT sepTypeStand = {half - 1, r3top + 2, half + 1, inner.bottom};
                                FillRect(hDC, &sepTypeStand, sepBr);
                                // sep col3|col4 — full height
                                RECT sep34 = {col4x - SEP_W / 2 - 1, inner.top, col4x - SEP_W / 2 + 1, inner.bottom};
                                FillRect(hDC, &sep34, sepBr);
                                // sep col4|col5 — full height
                                RECT sep45 = {col5x - SEP_W / 2 - 1, inner.top, col5x - SEP_W / 2 + 1, inner.bottom};
                                FillRect(hDC, &sep45, sepBr);
                                DeleteObject(sepBr);
                            }

                            auto drawCell3 = [&](int l, int r, UINT hAlign,
                                                 const char* t1, bool dim1,
                                                 const char* t2, bool dim2,
                                                 const char* t3, bool dim3)
                            {
                                RECT r1R = {l + 2, inner.top, r - 4, r2top};
                                RECT r2R = {l + 2, r2top,     r - 4, r3top};
                                RECT r3R = {l + 2, r3top,     r - 4, inner.bottom};
                                if (t1 && *t1)
                                {
                                    SetTextColor(hDC, dim1 ? s.textDim : s.text);
                                    DrawTextA(hDC, t1, -1, &r1R,
                                              hAlign | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                                }
                                if (t2 && *t2)
                                {
                                    SetTextColor(hDC, dim2 ? s.textDim : s.text);
                                    DrawTextA(hDC, t2, -1, &r2R,
                                              hAlign | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                                }
                                if (t3 && *t3)
                                {
                                    SetTextColor(hDC, dim3 ? s.textDim : s.text);
                                    DrawTextA(hDC, t3, -1, &r3R,
                                              hAlign | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                                }
                            };

                            SelectObject(hDC, bodyFont);
                            drawCell3(col3x, col4x - SEP_W, DT_RIGHT,
                                      sidShort.c_str(), true,
                                      "443",            false,
                                      adx.c_str(),      false);
                            drawCell3(col4x, col5x - SEP_W, DT_CENTER,
                                      "",         false,
                                      sqk.c_str(),false,
                                      "",         false);

                            // Col 5 flex: row3 runway right-aligned, dimmed, large font.
                            SelectObject(hDC, csFont);
                            SetTextColor(hDC, s.textDim);
                            RECT rwyRow3 = {col5x + 4, r3top, col5r, inner.bottom};
                            DrawTextA(hDC, rwy.c_str(), -1, &rwyRow3,
                                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        }

                        this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_STRIP, s.callsign.c_str(),
                                                  stripR, false, "");
                        sy += e.def->stackBottom ? -rowH : rowH;
                        ++drawn;
                    }

                    // "+N more" indicator if truncated
                    if (drawn < e.stripCount)
                    {
                        RECT moreR;
                        if (e.def->stackBottom)
                            moreR = {cx0 + 8, stripAreaTop, cx1 - 4, stripAreaTop + rowH};
                        else
                            moreR = {cx0 + 8, sy, cx1 - 4, std::min<int>(stripAreaBot, sy + rowH)};
                        SelectObject(hDC, bodyFont);
                        SetTextColor(hDC, RGB(200, 200, 200));
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "+%d more", e.stripCount - drawn);
                        DrawTextA(hDC, buf, -1, &moreR,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }

                cy += gh;
            }

            DeleteObject(csLargeFont);
            DeleteObject(bodyLargeFont);
            DeleteObject(stripSmallFont);
        }
    }

    // ── Status bar ───────────────────────────────────────────────────────────
    HFONT statusFont = CreateFontA(fs(fsStatus), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    SelectObject(hDC, statusFont);

    // UNDO button (left)
    SIZE szUndo = {};
    GetTextExtentPoint32A(hDC, "UNDO", 4, &szUndo);
    RECT undoRect = {statusRect.left + 6, statusRect.top + 3,
                     statusRect.left + 6 + szUndo.cx + 20, statusRect.bottom - 3};
    auto undoBrush = CreateSolidBrush(RGB(55, 55, 55));
    FillRect(hDC, &undoRect, undoBrush);
    DeleteObject(undoBrush);
    auto undoBorder = CreateSolidBrush(RGB(120, 120, 120));
    FrameRect(hDC, &undoRect, undoBorder);
    DeleteObject(undoBorder);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "UNDO", -1, &undoRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_UNDO_BTN, "UNDO", undoRect, false, "");

    // ATIS button (next to UNDO) — shows current letter for primary ICAO
    std::string atisLetter = "-";
    if (!myIcao.empty())
    {
        auto* timers = static_cast<CFlowX_Timers*>(this->GetPlugIn());
        std::string letter = timers->GetAtisLetter(myIcao);
        if (!letter.empty())
            atisLetter = letter;
    }
    SIZE szAtis = {};
    std::string atisLabelPre = "ATIS " + atisLetter;
    GetTextExtentPoint32A(hDC, atisLabelPre.c_str(), (int)atisLabelPre.size(), &szAtis);
    RECT atisRect = {undoRect.right + 6, undoRect.top, undoRect.right + 6 + szAtis.cx + 20, undoRect.bottom};
    auto atisBrush = CreateSolidBrush(RGB(55, 55, 55));
    FillRect(hDC, &atisRect, atisBrush);
    DeleteObject(atisBrush);
    auto atisBorder = CreateSolidBrush(RGB(120, 120, 120));
    FrameRect(hDC, &atisRect, atisBorder);
    DeleteObject(atisBorder);
    DrawTextA(hDC, atisLabelPre.c_str(), -1, &atisRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_ATIS_BTN, "ATIS", atisRect, false, "");

    // Clock (right) — HH:MM:SS UTC
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        SIZE szClock = {};
        GetTextExtentPoint32A(hDC, "00:00:00", 8, &szClock);
        RECT clockRect = {statusRect.right - szClock.cx - 12, statusRect.top,
                          statusRect.right - 12, statusRect.bottom};
        DrawTextA(hDC, buf, -1, &clockRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Resize handle (bottom-right corner) ─────────────────────────────────
    const int RESIZE_SZ = 10;
    RECT      resizeRect = {wx + WIN_W - RESIZE_SZ, wy + WIN_H - RESIZE_SZ, wx + WIN_W, wy + WIN_H};
    auto      resizeBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hDC, &resizeRect, resizeBrush);
    DeleteObject(resizeBrush);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_RESIZE, "DIFLIS_RESIZE", resizeRect, true, "");

    SelectObject(hDC, prevFont);
    DeleteObject(statusFont);
    DeleteObject(groupHdrFont);
    DeleteObject(groupSideFont);
}
/// @brief Creates the DIFLIS popout window, seeding position/size from settings or in-screen state.
void RadarScreen::CreateDiflisPopout(CFlowX_Settings* s)
{
    int w = s->GetDiflisPopoutW();
    int h = s->GetDiflisPopoutH();
    if (w <= 0)
        w = this->diflisWindowW;
    if (h <= 0)
        h = this->diflisWindowH;
    int x = s->GetDiflisPopoutX();
    int y = s->GetDiflisPopoutY();
    if (x == -1)
    {
        x = this->diflisWindowPos.x;
        y = this->diflisWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetDiflisPopoutPos(x, y);
    }
    this->diflisPopout = std::make_unique<PopoutWindow>(
        "DIFLIS", x, y, w, h,
        [s](int nx, int ny)
        { s->SetDiflisPopoutPos(nx, ny); },
        nullptr,
        [](const HitArea& ha, POINT delta, int currentW, int currentH) -> std::pair<int, int>
        {
            if (ha.objectId != "DIFLIS_RESIZE")
                return {0, 0};
            return {std::max(520, currentW + (int)delta.x), std::max(360, currentH + (int)delta.y)};
        });

    // Apply persisted topmost + maximized states.
    if (!s->GetDiflisPopoutTopmost())
        this->diflisPopout->SetTopmost(false);
    if (s->GetDiflisPopoutMaximized())
        this->diflisPopout->SetMaximized(true);
}
