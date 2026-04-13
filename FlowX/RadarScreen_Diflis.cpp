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

    // ── Cache airport DIFLIS config once (config is not hot-swappable). ─────────
    // All per-draw reads below use these cached values instead of walking FindMyAirport /
    // dereferencing dcfg every frame.
    static const DiflisAirportConfig* s_dcfg = nullptr;
    static std::string                s_myIcao;
    static bool                       s_cfgResolved = false;
    if (!s_cfgResolved)
    {
        auto apIt0 = settingsD->FindMyAirport();
        if (apIt0 != settingsD->GetAirports().end())
        {
            s_dcfg   = &apIt0->second.diflis;
            s_myIcao = apIt0->first;
        }
        s_cfgResolved = true;
    }
    const DiflisAirportConfig* dcfg   = s_dcfg;
    const std::string&         myIcao = s_myIcao;

    static const int s_fsStatus  = dcfg ? dcfg->fontSizeStatusBar : 20;
    static const int s_fsHdr     = dcfg ? dcfg->fontSizeGroupHeader : 16;
    static const int s_fsHdrSide = dcfg ? dcfg->fontSizeGroupSide : 14;
    static const int s_fsLarge   = dcfg ? dcfg->fontSizeStripLarge : 30;
    static const int s_fsMedium  = dcfg ? dcfg->fontSizeStripMedium : 20;
    static const int s_fsSmall   = dcfg ? dcfg->fontSizeStripSmall : 16;

    const int statusFontH = std::abs(fs(s_fsStatus));
    const int STATUS_H    = std::max({28, (int)std::round(36 * scale), statusFontH * 2 + 8});

    // Consolas is monospace — text width is deterministic from pixel height (~0.55 aspect).
    // Replace GDI text-measurement calls with this arithmetic helper so no per-draw
    // GetTextExtentPoint32A is needed for sizing cells or buttons.
    auto monoW = [](int fontHPx, int chars) -> int
    {
        int h = std::abs(fontHPx);
        return chars * std::max(1, (h * 55 + 50) / 100);
    };

    // ── Title bar + close/popout buttons ─────────────────────────────────────
    RECT xRect   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT popRect = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};

    // Maximize + topmost toggles; only visible when this is a popout render (the two
    // buttons live 4px to the left of the popin button, separated by the gap).
    constexpr int GAP     = 4;
    RECT          topRect = {0, 0, 0, 0};
    RECT          maxRect = {0, 0, 0, 0};
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
    bool xHovered    = PtInRect(&xRect, cursorD) != 0;
    bool popHovered  = PtInRect(&popRect, cursorD) != 0;
    bool topHovered  = this->isPopoutRender_ && PtInRect(&topRect, cursorD) != 0;
    bool maxHovered  = this->isPopoutRender_ && PtInRect(&maxRect, cursorD) != 0;
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

    RECT statusSep   = {wx, statusRect.top - 3, wx + WIN_W, statusRect.top};
    auto statusSepBr = CreateSolidBrush(RGB(0, 0, 0));
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

    // ── Column layout ────────────────────────────────────────────────────────
    const int NUM_COLS = 4;
    const int bodyX0   = bodyRect.left + PAD;
    const int bodyY0   = bodyRect.top + PAD;
    const int bodyX1   = bodyRect.right - PAD;
    const int bodyY1   = bodyRect.bottom - PAD;
    const int bodyW    = std::max(1, bodyX1 - bodyX0);
    const int bodyH    = std::max(1, bodyY1 - bodyY0);
    const int COL_GAP  = 8;

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
    colX[0]         = bodyX0;
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
    const int fsStatus  = s_fsStatus;
    const int fsHdr     = s_fsHdr;
    const int fsHdrSide = s_fsHdrSide;
    const int fsLarge   = s_fsLarge;
    const int fsMedium  = s_fsMedium;
    const int fsSmall   = s_fsSmall;

    HFONT groupHdrFont  = mkFont(fs(fsHdr), FW_BOLD);
    HFONT groupSideFont = mkFont(fs(fsHdrSide), FW_BOLD);

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
        case DiflisStripVariant::Collapsed:
            return std::max(20, (int)std::round(50 * s));
        case DiflisStripVariant::Expanded:
            return std::max(40, (int)std::round(110 * s));
        }
        return 22;
    };
    auto isExpandedVariant = [](DiflisStripVariant v)
    { return v == DiflisStripVariant::Expanded; };

    // ── Per-column layout: active groups, weights, rects ─────────────────────
    this->diflisGroupRects.clear();
    RECT    dragSrcR        = {0, 0, 0, 0};
    POINT   dragCenterOff   = {0, 0};
    HBITMAP dragCaptureBmp  = nullptr;
    bool    dragSrcCaptured = false;
    if (dcfg)
    {
        for (int c = 0; c < NUM_COLS; ++c)
        {
            struct Entry
            {
                const DiflisGroupDef* def;
                int                   stripCount;
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

            int cx0  = colX[c];
            int cx1  = colX[c + 1] - (c < NUM_COLS - 1 ? COL_GAP : 0);
            int cy   = bodyY0;
            int colH = bodyH;

            // Per-column strip fonts, sized from the column's actual strip width.
            float     widthScale     = std::clamp(float(cx1 - cx0 - 8) / STRIP_WIDTH_BASELINE, 0.35f, 2.0f);
            const int csFontPx       = fs((int)std::round(fsLarge * widthScale));
            const int bodyFontPx     = fs((int)std::round(fsMedium * widthScale));
            const int smallFontPx    = fs((int)std::round(fsSmall * widthScale));
            HFONT     csLargeFont    = mkFont(csFontPx, FW_BOLD);
            HFONT     bodyLargeFont  = mkFont(bodyFontPx, FW_NORMAL);
            HFONT     stripSmallFont = mkFont(smallFontPx, FW_NORMAL);
            (void)stripSmallFont;

            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& e  = entries[i];
                float       w  = std::max(0.01f, e.def->heightWeight);
                int         gh = (i + 1 == entries.size()) ? (bodyY0 + colH - cy)
                                                           : (int)std::round(colH * (w / totalWeight));
                if (gh < 18)
                    gh = 18;

                RECT groupRect = {cx0, cy, cx1, cy + gh};
                if (e.def->acceptsDrop)
                    this->diflisGroupRects.emplace_back(e.def->id, groupRect);
                // Group body background
                auto gBrush = CreateSolidBrush(RGB(40, 40, 40));
                FillRect(hDC, &groupRect, gBrush);
                DeleteObject(gBrush);
                auto gBorder = CreateSolidBrush(RGB(70, 70, 70));
                FrameRect(hDC, &groupRect, gBorder);
                DeleteObject(gBorder);

                // Group header bar (+4 for 2 px padding above and below the title text)
                const int HDR_H    = std::max(14, (int)std::round(16 * scale)) + 4;
                RECT      hdrRect  = {cx0, cy, cx1, cy + HDR_H};
                auto      hdrBrush = CreateSolidBrush(RGB(15, 15, 15));
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
                    int                rowH    = variantH(chosen, widthScale);
                    int                visible = e.stripCount;

                    if (rowH * visible > avail)
                    {
                        // Preferred doesn't fit — try fallback
                        DiflisStripVariant fb  = e.def->fallbackVariant;
                        int                fbH = variantH(fb, widthScale);
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
                        auto     sBrush = CreateSolidBrush(bg);
                        FillRect(hDC, &stripR, sBrush);
                        DeleteObject(sBrush);

                        // 2px border kept as plain background; all content inset inside it.
                        RECT inner = {stripR.left + 2, stripR.top + 2, stripR.right - 2, stripR.bottom - 2};

                        // Callsign cell width sized to fit "WZZ2929" in the large font. In the expanded
                        // variant the same horizontal block also hosts type+stand in its bottom third,
                        // so widen it if needed to fit "TYPE STND" underneath the callsign.
                        SelectObject(hDC, csFont);
                        int csW = monoW(csFontPx, 7) + 8;
                        if (expandedV)
                        {
                            csW = std::max(csW, monoW(bodyFontPx, 9) + 12);
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
                        case 'H':
                        case 'h':
                            csLabel += "+";
                            break;
                        case 'J':
                        case 'j':
                            csLabel += "^";
                            break;
                        case 'L':
                        case 'l':
                            csLabel += "-";
                            break;
                        default:
                            break;
                        }
                        {
                            RECT  csTxt    = {csDarkR.left + 6, csDarkR.top, csDarkR.right - 4, csDarkR.bottom};
                            int   avail    = csTxt.right - csTxt.left;
                            int   lblW     = monoW(csFontPx, (int)csLabel.size());
                            HFONT csShrunk = nullptr;
                            if (lblW > avail && avail > 0)
                            {
                                float ratio = (float)avail / (float)lblW;
                                int   baseH = std::abs(csFontPx);
                                int   newH  = -std::max(6, (int)std::round(baseH * ratio));
                                csShrunk    = mkFont(newH, FW_BOLD);
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
                        int  stW    = std::max(1, variantH(DiflisStripVariant::Expanded, widthScale) - 3 - 4);
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
                            int typeW  = monoW(bodyFontPx, 6) + 10;
                            int standW = monoW(bodyFontPx, 3) + 10;

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
                                sidShort     = route;
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
                        else // Expanded
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
                            int  csBotTop  = r3top;
                            int  half      = csR.left + (csR.right - csR.left) / 2;
                            RECT typeBotR  = {csR.left + 6, csBotTop, half - 2, inner.bottom};
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
                                sidShort     = route;
                                size_t digit = 0;
                                while (digit < sidShort.size() &&
                                       !std::isdigit((unsigned char)sidShort[digit]))
                                    ++digit;
                                if (digit > 3)
                                    sidShort = sidShort.substr(0, 3) + sidShort.substr(digit);
                            }

                            // Column widths (content-sized via body font)
                            SelectObject(hDC, bodyFont);
                            const int SEP_W = 4;
                            int       col3W = monoW(bodyFontPx, 5) + 10;
                            int       col4W = monoW(bodyFontPx, 4) + 10;
                            int       col3x = csR.right + SEP_W;
                            int       col4x = col3x + col3W + SEP_W;
                            int       col5x = col4x + col4W + SEP_W;
                            int       col5r = stBtnR.left - SEP_W;

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
                                RECT r2R = {l + 2, r2top, r - 4, r3top};
                                RECT r3R = {l + 2, r3top, r - 4, inner.bottom};
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
                                      "443", false,
                                      adx.c_str(), false);
                            drawCell3(col4x, col5x - SEP_W, DT_CENTER,
                                      "", false,
                                      sqk.c_str(), false,
                                      "", false);

                            // Col 5 flex: row3 runway right-aligned, dimmed, large font.
                            SelectObject(hDC, csFont);
                            SetTextColor(hDC, s.textDim);
                            RECT rwyRow3 = {col5x + 4, r3top, col5r, inner.bottom};
                            DrawTextA(hDC, rwy.c_str(), -1, &rwyRow3,
                                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        }

                        this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_STRIP, s.callsign.c_str(),
                                                  csDarkR, true, "");
                        if (!this->diflisDragCallsign.empty() && s.callsign == this->diflisDragCallsign)
                        {
                            dragSrcR        = stripR;
                            dragSrcCaptured = true;
                            dragCenterOff.x = (csDarkR.left + csDarkR.right) / 2 - stripR.left;
                            dragCenterOff.y = (csDarkR.top + csDarkR.bottom) / 2 - stripR.top;

                            // Capture strip pixels before blanking the slot.
                            int srcW = stripR.right - stripR.left;
                            int srcH = stripR.bottom - stripR.top;
                            if (srcW > 0 && srcH > 0)
                            {
                                dragCaptureBmp = CreateCompatibleBitmap(hDC, srcW, srcH);
                                HDC     capDC  = CreateCompatibleDC(hDC);
                                HGDIOBJ oldCap = SelectObject(capDC, dragCaptureBmp);
                                BitBlt(capDC, 0, 0, srcW, srcH, hDC, stripR.left, stripR.top, SRCCOPY);
                                SelectObject(capDC, oldCap);
                                DeleteDC(capDC);
                            }

                            // Blank the original slot with the group body colour so neighbours don't reflow.
                            auto blankBr = CreateSolidBrush(RGB(40, 40, 40));
                            FillRect(hDC, &stripR, blankBr);
                            DeleteObject(blankBr);
                        }
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

    // QNH label (far left, number only — strip leading "Q"/"A")
    std::string qnhLabel = "-";
    if (!myIcao.empty())
    {
        auto*       timers = static_cast<CFlowX_Timers*>(this->GetPlugIn());
        std::string raw    = timers->GetAirportQnh(myIcao);
        if (raw.size() > 1 && (raw[0] == 'Q' || raw[0] == 'A'))
            qnhLabel = raw.substr(1);
        else if (!raw.empty())
            qnhLabel = raw;
    }
    int  qnhW    = monoW(fs(fsStatus), (int)qnhLabel.size());
    RECT qnhRect = {statusRect.left + 8, statusRect.top,
                    statusRect.left + 8 + qnhW, statusRect.bottom};
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, qnhLabel.c_str(), -1, &qnhRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // UNDO button (after QNH) — padding and vertical margin scale with status font size
    const int btnHPad   = std::max(20, statusFontH * 3 / 2); // total horizontal padding
    const int btnVPad   = std::max(6, statusFontH / 2);      // vertical margin each side
    const int btnTop    = statusRect.top + btnVPad;
    const int btnBot    = statusRect.bottom - btnVPad;
    const int undoTextW = monoW(fs(fsStatus), 4);
    RECT      undoRect  = {qnhRect.right + 10, btnTop,
                           qnhRect.right + 10 + undoTextW + btnHPad, btnBot};
    auto      undoBrush = CreateSolidBrush(RGB(55, 55, 55));
    FillRect(hDC, &undoRect, undoBrush);
    DeleteObject(undoBrush);
    auto undoBorder = CreateSolidBrush(RGB(0, 0, 0));
    FrameRect(hDC, &undoRect, undoBorder);
    DeleteObject(undoBorder);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "UNDO", -1, &undoRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_UNDO_BTN, "UNDO", undoRect, false, "");

    // ATIS button (next to UNDO) — shows current letter for primary ICAO
    std::string atisLetter = "-";
    if (!myIcao.empty())
    {
        auto*       timers = static_cast<CFlowX_Timers*>(this->GetPlugIn());
        std::string letter = timers->GetAtisLetter(myIcao);
        if (!letter.empty())
            atisLetter = letter;
    }
    int       undoW     = undoRect.right - undoRect.left;
    const int btnGap    = std::max(6, statusFontH / 3);
    RECT      atisRect  = {undoRect.right + btnGap, undoRect.top,
                           undoRect.right + btnGap + undoW, undoRect.bottom};
    auto      atisBrush = CreateSolidBrush(RGB(55, 55, 55));
    FillRect(hDC, &atisRect, atisBrush);
    DeleteObject(atisBrush);
    auto atisBorder = CreateSolidBrush(RGB(0, 0, 0));
    FrameRect(hDC, &atisRect, atisBorder);
    DeleteObject(atisBorder);
    DrawTextA(hDC, atisLetter.c_str(), -1, &atisRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Three small 3-char buttons (INF / FPL / VEH) — placeholder, not yet wired.
    // Leading gap is double the QNH↔UNDO spacing (20 px scales to 2*btnGap*≈).
    const int   smallW     = monoW(fs(fsStatus), 3) + btnHPad;
    const char* labels3[3] = {"INF", "FPL", "VEH"};
    int         leftX      = atisRect.right + btnGap * 2 + 4;
    for (int i = 0; i < 3; ++i)
    {
        RECT r  = {leftX, atisRect.top, leftX + smallW, atisRect.bottom};
        auto bg = CreateSolidBrush(RGB(55, 55, 55));
        FillRect(hDC, &r, bg);
        DeleteObject(bg);
        auto bd = CreateSolidBrush(RGB(0, 0, 0));
        FrameRect(hDC, &r, bd);
        DeleteObject(bd);
        DrawTextA(hDC, labels3[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        leftX = r.right + btnGap;
    }
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_ATIS_BTN, "ATIS", atisRect, false, "");

    // ── Controller position status buttons (AMS N / AMS S / DLV / GDE / GDW / TWE / TWW) ──
    // Each entry has a label and the primary frequency of the position. Buttons turn green
    // when any online controller is tuned to that frequency, red otherwise. Entries with
    // freq == 0 are unknown positions → always offline.
    struct PosBtn
    {
        const char* label;
        double      freq;
    };
    static const PosBtn posBtns[] = {
        {"AMS N", 0.000},
        {"AMS S", 0.000},
        {"DLV", 122.125},
        {"GDE", 121.600},
        {"GDW", 121.775},
        {"TWE", 119.400},
        {"TWW", 123.800},
    };

    // Snapshot online controller frequencies once for this draw.
    std::vector<double> onlineFreqs;
    onlineFreqs.reserve(16);
    auto* plugIn = this->GetPlugIn();
    for (auto c = plugIn->ControllerSelectFirst(); c.IsValid(); c = plugIn->ControllerSelectNext(c))
    {
        if (c.IsController() && c.GetRating() > 1)
            onlineFreqs.push_back(c.GetPrimaryFrequency());
    }
    double myFreq = 0.0;
    {
        auto me = plugIn->ControllerMyself();
        if (me.IsValid() && me.IsController())
            myFreq = me.GetPrimaryFrequency();
    }
    auto freqMatches = [](double a, double b) -> bool
    { return a > 0.0 && b > 0.0 && std::abs(a - b) < 0.005; };
    auto freqOnline = [&](double f) -> bool
    {
        if (f <= 0.0)
            return false;
        for (double of : onlineFreqs)
        {
            if (freqMatches(of, f))
                return true;
        }
        return false;
    };

    constexpr COLORREF COL_YELLOW = RGB(250, 225, 70);
    constexpr COLORREF COL_GREEN  = RGB(90, 220, 90);
    constexpr COLORREF COL_RED    = RGB(230, 90, 90);

    auto resolveColor = [&](double f) -> COLORREF
    {
        if (freqMatches(myFreq, f))
            return COL_YELLOW;
        if (freqOnline(f))
            return COL_GREEN;
        return COL_RED;
    };

    // First pass: each button's own color.
    constexpr int N_POS = sizeof(posBtns) / sizeof(posBtns[0]);
    COLORREF      colors[N_POS];
    for (int i = 0; i < N_POS; ++i)
        colors[i] = resolveColor(posBtns[i].freq);

    // Second pass: apply cross-coupling. For any pair {covering, covered} where the
    // covering freq is not offline, the covered button inherits the covering button's color.
    if (dcfg)
    {
        for (const auto& cc : dcfg->crossCouple)
        {
            COLORREF coveringColor = resolveColor(cc.first);
            if (coveringColor == COL_RED)
                continue;
            for (int i = 0; i < N_POS; ++i)
            {
                if (freqMatches(posBtns[i].freq, cc.second))
                    colors[i] = coveringColor;
            }
        }
    }

    // Tight gap within the position-buttons group (half the normal button gap).
    const int tightGap = std::max(2, btnGap / 2);
    int       posLeftX = leftX + btnGap * 2 + 4;

    auto drawBtn = [&](int& x, const char* label, COLORREF fill, COLORREF textColor) -> RECT
    {
        int  labelLen = (int)std::strlen(label);
        int  w        = monoW(fs(fsStatus), labelLen) + btnHPad;
        RECT r        = {x, atisRect.top, x + w, atisRect.bottom};
        auto bg       = CreateSolidBrush(fill);
        FillRect(hDC, &r, bg);
        DeleteObject(bg);
        auto bd = CreateSolidBrush(RGB(0, 0, 0));
        FrameRect(hDC, &r, bd);
        DeleteObject(bd);
        SetTextColor(hDC, textColor);
        DrawTextA(hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x = r.right;
        return r;
    };

    for (int i = 0; i < N_POS; ++i)
    {
        drawBtn(posLeftX, posBtns[i].label, colors[i], TAG_COLOR_BLACK);
        posLeftX += tightGap;
    }
    // Blue "+" at the end of the position group (same tight gap as the rest of the group).
    drawBtn(posLeftX, "+", RGB(70, 140, 220), TAG_COLOR_WHITE);
    posLeftX += btnGap;

    // Next group: black SEND — normal gap between +/SEND, then a large gap before RED.
    drawBtn(posLeftX, "SEND", RGB(55, 55, 55), TAG_COLOR_WHITE);
    posLeftX += btnGap * 2 + 4;

    // Final group: RED / TRASH / TWSUP with normal gaps.
    drawBtn(posLeftX, "RED", RGB(55, 55, 55), TAG_COLOR_WHITE);
    posLeftX += btnGap;
    drawBtn(posLeftX, "TRASH", RGB(250, 225, 70), TAG_COLOR_BLACK);
    posLeftX += btnGap;
    drawBtn(posLeftX, "TWSUP", RGB(55, 55, 55), TAG_COLOR_WHITE);

    // Clock (right) — HH:MM:SS UTC
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        const int clockW    = monoW(fs(fsStatus), 8);
        RECT      clockRect = {statusRect.right - clockW - 12, statusRect.top,
                               statusRect.right - 12, statusRect.bottom};
        DrawTextA(hDC, buf, -1, &clockRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // ── Resize handle (bottom-right corner) ─────────────────────────────────
    const int RESIZE_SZ   = 10;
    RECT      resizeRect  = {wx + WIN_W - RESIZE_SZ, wy + WIN_H - RESIZE_SZ, wx + WIN_W, wy + WIN_H};
    auto      resizeBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hDC, &resizeRect, resizeBrush);
    DeleteObject(resizeBrush);
    this->AddScreenObjectAuto(SCREEN_OBJECT_DIFLIS_RESIZE, "DIFLIS_RESIZE", resizeRect, true, "");

    // ── Drag ghost overlay (drawn last so it floats above everything) ────────
    if (this->isPopoutRender_ && this->currentPopout_)
    {
        if (dragSrcCaptured && dragCaptureBmp)
        {
            int               srcW = dragSrcR.right - dragSrcR.left;
            int               srcH = dragSrcR.bottom - dragSrcR.top;
            std::vector<RECT> dropRects;
            dropRects.reserve(this->diflisGroupRects.size());
            for (const auto& gr : this->diflisGroupRects)
                dropRects.push_back(gr.second);
            this->currentPopout_->SetDragOverlay(dragCaptureBmp, srcW, srcH, dragCenterOff,
                                                 std::move(dropRects));
            dragCaptureBmp = nullptr; // ownership transferred to popout
        }
        else if (this->diflisDragCallsign.empty())
        {
            this->currentPopout_->ClearDragOverlay();
        }
    }
    else if (!this->diflisDragCallsign.empty() && this->diflisDragCursor.x > -1000 &&
             this->diflisDragCursor.y > -1000)
    {
        for (const auto& gr : this->diflisGroupRects)
        {
            if (PtInRect(&gr.second, this->diflisDragCursor))
            {
                auto hiBr = CreateSolidBrush(RGB(90, 160, 220));
                // 2 px frame drawn as 4 filled rects for a thicker highlight.
                RECT g = gr.second;
                RECT t = {g.left, g.top, g.right, g.top + 2};
                RECT b = {g.left, g.bottom - 2, g.right, g.bottom};
                RECT l = {g.left, g.top, g.left + 2, g.bottom};
                RECT r = {g.right - 2, g.top, g.right, g.bottom};
                FillRect(hDC, &t, hiBr);
                FillRect(hDC, &b, hiBr);
                FillRect(hDC, &l, hiBr);
                FillRect(hDC, &r, hiBr);
                DeleteObject(hiBr);
                break;
            }
        }

        if (dragSrcCaptured && dragCaptureBmp)
        {
            int     srcW = dragSrcR.right - dragSrcR.left;
            int     srcH = dragSrcR.bottom - dragSrcR.top;
            HDC     sDC  = CreateCompatibleDC(hDC);
            HGDIOBJ oB   = SelectObject(sDC, dragCaptureBmp);
            int     dstX = this->diflisDragCursor.x - dragCenterOff.x;
            int     dstY = this->diflisDragCursor.y - dragCenterOff.y;
            BitBlt(hDC, dstX, dstY, srcW, srcH, sDC, 0, 0, SRCCOPY);
            SelectObject(sDC, oB);
            DeleteDC(sDC);
        }
    }

    if (dragCaptureBmp)
        DeleteObject(dragCaptureBmp);

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

void RadarScreen::DiflisMoveStrip(const std::string& callsign, const std::string& fromGroup, const std::string& toGroup)
{
    if (callsign.empty() || toGroup.empty() || fromGroup == toGroup)
        return;

    DiflisUndoEntry entry;
    entry.callsign  = callsign;
    entry.fromGroup = fromGroup;
    entry.toGroup   = toGroup;

    DiflisMutation m;
    m.field = DiflisMutationField::DiflisOverride;
    auto it = this->diflisOverrides.find(callsign);
    if (it != this->diflisOverrides.end())
    {
        m.prevExisted = true;
        m.prevValue   = it->second;
    }
    entry.mutations.push_back(m);

    // DEPARTURES is the auto-derived home for airborne strips — dropping there clears the override.
    if (toGroup == "DEPARTURES")
        this->diflisOverrides.erase(callsign);
    else
        this->diflisOverrides[callsign] = toGroup;

    this->diflisUndoStack.push_back(std::move(entry));
    while (this->diflisUndoStack.size() > 32)
        this->diflisUndoStack.pop_front();
}

void RadarScreen::DiflisUndo()
{
    if (this->diflisUndoStack.empty())
        return;
    DiflisUndoEntry entry = std::move(this->diflisUndoStack.back());
    this->diflisUndoStack.pop_back();

    for (auto it = entry.mutations.rbegin(); it != entry.mutations.rend(); ++it)
    {
        switch (it->field)
        {
        case DiflisMutationField::DiflisOverride:
            if (it->prevExisted)
                this->diflisOverrides[entry.callsign] = it->prevValue;
            else
                this->diflisOverrides.erase(entry.callsign);
            break;
        default:
            break; // Other fields deferred until real writeback lands.
        }
    }
}
