/**
 * @file RadarScreen_SettingsWindow.cpp
 * @brief RadarScreen partial implementation: Settings window (4:3 fixed-size popout with grouped toggles).
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "RadarScreen.h"
#include "CFlowX_Base.h"
#include "CFlowX_Settings.h"
#include "PopoutWindow.h"
#include "constants.h"
#include "helpers.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace
{
/// Fixed content size of the Settings window — roughly 4:3. Not resizable.
constexpr int SETTINGS_WIN_W = 560;
constexpr int SETTINGS_WIN_H = 420;

/// A single row inside a group box.
struct SettingsRow
{
    const char* label;       ///< Text drawn to the right of the checkbox / next to the slider
    int         menuIdx;     ///< Dispatched via SCREEN_OBJECT_START_MENU_ITEM and reused menu-dispatch handler
    bool        hasCheckbox; ///< True for toggle rows; false for action rows and slider rows
    bool        checked;     ///< Initial checkbox state (read at build time)
    bool        disabled;    ///< Greyed out, not clickable
    bool        isSlider;    ///< True for Fonts / BG opacity rows
    int         sliderMinusIdx; ///< Menu idx dispatched when the - button is clicked
    int         sliderPlusIdx;  ///< Menu idx dispatched when the + button is clicked
    std::string sliderValueText; ///< Rendered value to show between label and buttons
};

struct SettingsGroup
{
    const char*              title;
    std::vector<SettingsRow> rows;
};
} // namespace

void RadarScreen::DrawSettingsWindow(HDC hDC)
{
    auto* settings = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    auto* base     = static_cast<CFlowX_Base*>(this->GetPlugIn());
    if (!settings->GetSettingsVisible())
        return;

    const int TITLE_H = 13;
    const int X_BTN   = 11;
    const int op      = settings->GetBgOpacity();
    const int WIN_W   = SETTINGS_WIN_W;
    const int WIN_H   = SETTINGS_WIN_H;

    if (this->settingsWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->settingsWindowPos.x = (clip.left + clip.right) / 2 - WIN_W / 2;
        this->settingsWindowPos.y = (clip.top + clip.bottom) / 2 - WIN_H / 2;
    }

    int wx = this->settingsWindowPos.x;
    int wy = this->settingsWindowPos.y;

    // ── Cursor (live for in-screen, popout-provided for popout render) ──
    POINT cursor = {-9999, -9999};
    if (this->isPopoutRender_)
        cursor = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursor);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursor);
    }

    // ── Title bar rects ──
    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT xRect     = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    RECT popRect   = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    bool xHovered  = PtInRect(&xRect, cursor) != 0;
    bool popHovered = PtInRect(&popRect, cursor) != 0;

    // ── Background ──
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

    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    RECT dragRect = {wx, wy, popRect.left - 1, wy + TITLE_H};
    DrawTextA(hDC, "FLOWX SETTINGS", -1, &dragRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRect = popRect;
    if (!this->isPopoutRender_)
        popDrawRect.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prevFont);
    DeleteObject(titleFont);

    this->AddScreenObjectAuto(SCREEN_OBJECT_SETTINGS_WIN, "SETTINGS", dragRect, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "settings", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "settings", popRect, false, "");

    // ── Build group content (live-read on each paint so toggles reflect immediately) ──
    const int fo   = settings->GetFontOffset();
    const int bgOp = settings->GetBgOpacity();

    const std::string fontValStr = (fo > 0) ? std::format("+{}", fo) : std::format("{}", fo);
    const std::string bgValStr   = std::format("{}%", bgOp);

    std::vector<SettingsGroup> groups;
    groups.push_back({"Assists",
                      {{"Auto-Restore FPLN", 3, true, settings->GetAutoRestore(), false, false, 0, 0, {}},
                       {"Auto PARK", 15, true, settings->GetAutoParked(), false, false, 0, 0, {}},
                       {"Auto-Clear Scratch", 18, true, settings->GetAutoScratchpadClear(), false, false, 0, 0, {}},
                       {"HP auto-scratch", 28, true, settings->GetHpAutoScratch(), false, false, 0, 0, {}}}});
    groups.push_back({"Notifications",
                      {{"Airborne", 19, true, settings->GetSoundAirborne(), false, false, 0, 0, {}},
                       {"GND Transfer", 20, true, settings->GetSoundGndTransfer(), false, false, 0, 0, {}},
                       {"Ready T/O", 21, true, settings->GetSoundReadyTakeoff(), false, false, 0, 0, {}},
                       {"No Route", 30, true, settings->GetSoundNoRoute(), false, false, 0, 0, {}},
                       {"Taxi Conflict", 27, true, settings->GetSoundTaxiConflict(), false, false, 0, 0, {}}}});
    groups.push_back({"Options",
                      {{"Debug mode", 2, true, base->GetDebug(), false, false, 0, 0, {}},
                       {"Update check", 13, true, settings->GetUpdateCheck(), false, false, 0, 0, {}},
                       {"Flash messages", 14, true, settings->GetFlashOnMessage(), false, false, 0, 0, {}},
                       {"Appr Est Colors", 17, true, settings->GetApprEstColors(), false, false, 0, 0, {}},
                       {"Fonts", -1, false, false, false, true, 8, 9, fontValStr},
                       {"BG opacity", -1, false, false, false, true, 10, 11, bgValStr}}});
    const bool osmBusy = settings->IsOsmBusy();
    groups.push_back({"Taxi",
                      {{"Update TAXI info", 22, false, false, osmBusy, false, 0, 0, {}},
                       {"Clear TAXI routes", 26, false, false, false, false, 0, 0, {}},
                       {"Show TAXI network", 23, true, settings->GetShowTaxiOverlay(), false, false, 0, 0, {}},
                       {"Show TAXI labels", 24, true, settings->GetShowTaxiLabels(), false, false, 0, 0, {}},
                       {"Show TAXI graph", 29, true, settings->GetShowTaxiGraph(), false, false, 0, 0, {}},
                       {"Show TAXI routes", 25, true, settings->GetShowTaxiRoutes(), false, false, 0, 0, {}},
                       {"Log TAXI tests", 31, true, settings->GetLogTaxiTests(), false, false, 0, 0, {}}}});

    // ── 2×2 grid layout inside the window body ──
    const int PAD       = 12;
    const int GROUP_GAP = 10;
    const int BODY_TOP  = wy + TITLE_H + PAD;
    const int BODY_W    = WIN_W - 2 * PAD;
    const int BODY_H    = WIN_H - (TITLE_H + 2 * PAD);
    const int GROUP_W   = (BODY_W - GROUP_GAP) / 2;
    const int GROUP_H   = (BODY_H - GROUP_GAP) / 2;
    const int HDR_INSET = 8;  ///< Horizontal gap around the group-box title inset into the top border
    const int ITEM_H    = 20 + fo;
    const int CBX_S     = 9 + fo;
    const int CBX_GAP   = 5;

    HFONT hdrFont = CreateFontA(-11 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT itemFont = CreateFontA(-14 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT cbxFont  = CreateFontA(-11 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

    auto drawGroup = [&](const SettingsGroup& g, int gx, int gy)
    {
        RECT box = {gx, gy, gx + GROUP_W, gy + GROUP_H};

        // Border frame with a small cut-out at the title so the label sits "inside" the top edge.
        prevFont = (HFONT)SelectObject(hDC, hdrFont);
        SIZE titleSz{};
        GetTextExtentPoint32A(hDC, g.title, (int)strlen(g.title), &titleSz);
        const int titleBgLeft  = gx + HDR_INSET - 2;
        const int titleBgRight = titleBgLeft + titleSz.cx + 4;
        const int titleMidY    = gy;

        auto frameBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
        // Top edge split around the title label
        RECT topL = {gx, titleMidY, titleBgLeft, titleMidY + 1};
        RECT topR = {titleBgRight, titleMidY, gx + GROUP_W, titleMidY + 1};
        FillRect(hDC, &topL, frameBrush);
        FillRect(hDC, &topR, frameBrush);
        RECT bot = {gx, gy + GROUP_H - 1, gx + GROUP_W, gy + GROUP_H};
        RECT lft = {gx, titleMidY, gx + 1, gy + GROUP_H};
        RECT rgt = {gx + GROUP_W - 1, titleMidY, gx + GROUP_W, gy + GROUP_H};
        FillRect(hDC, &bot, frameBrush);
        FillRect(hDC, &lft, frameBrush);
        FillRect(hDC, &rgt, frameBrush);
        DeleteObject(frameBrush);

        SetTextColor(hDC, TAG_COLOR_WHITE);
        RECT titleR = {titleBgLeft + 2, gy - titleSz.cy / 2,
                       titleBgRight, gy - titleSz.cy / 2 + titleSz.cy};
        DrawTextA(hDC, g.title, -1, &titleR, DT_LEFT | DT_TOP | DT_SINGLELINE);

        // Rows
        SelectObject(hDC, itemFont);
        int iy = gy + 10 + titleSz.cy / 2;
        for (const auto& r : g.rows)
        {
            RECT rowRect = {gx + 6, iy, gx + GROUP_W - 6, iy + ITEM_H};
            bool hov     = !r.disabled && PtInRect(&rowRect, cursor) != 0;
            if (hov)
            {
                auto hoverBrush = CreateSolidBrush(RGB(45, 70, 115));
                FillRect(hDC, &rowRect, hoverBrush);
                DeleteObject(hoverBrush);
            }

            if (r.hasCheckbox)
            {
                int  cy      = iy + (ITEM_H - CBX_S) / 2;
                int  cx      = gx + 10;
                RECT boxRect = {cx, cy, cx + CBX_S, cy + CBX_S};

                auto boxBrush = CreateSolidBrush(r.checked ? RGB(30, 55, 95) : RGB(25, 25, 25));
                FillRect(hDC, &boxRect, boxBrush);
                DeleteObject(boxBrush);

                auto boxBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &boxRect, boxBorder);
                DeleteObject(boxBorder);

                if (r.checked)
                {
                    SelectObject(hDC, cbxFont);
                    SetTextColor(hDC, TAG_COLOR_WHITE);
                    DrawTextA(hDC, "x", -1, &boxRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hDC, itemFont);
                }

                RECT lblRect = {cx + CBX_S + CBX_GAP, iy, rowRect.right, iy + ITEM_H};
                SetTextColor(hDC, r.disabled ? RGB(70, 70, 70)
                                  : hov      ? TAG_COLOR_WHITE
                                             : TAG_COLOR_LIST_GRAY);
                DrawTextA(hDC, r.label, -1, &lblRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                if (!r.disabled)
                {
                    auto objId = std::format("MENU|{}", r.menuIdx);
                    this->AddScreenObjectAuto(SCREEN_OBJECT_START_MENU_ITEM, objId.c_str(),
                                              rowRect, false, r.label);
                }
            }
            else if (r.isSlider)
            {
                const int BTN_W    = 14 + fo;
                RECT      minusR   = {rowRect.right - BTN_W * 2 - 4, iy + (ITEM_H - BTN_W) / 2,
                                      rowRect.right - BTN_W - 4, iy + (ITEM_H + BTN_W) / 2};
                RECT      plusR    = {rowRect.right - BTN_W, iy + (ITEM_H - BTN_W) / 2,
                                      rowRect.right, iy + (ITEM_H + BTN_W) / 2};
                bool      minusHov = PtInRect(&minusR, cursor) != 0;
                bool      plusHov  = PtInRect(&plusR, cursor) != 0;

                for (auto [rect, bh] : {std::pair{minusR, minusHov}, std::pair{plusR, plusHov}})
                {
                    auto btnBrush = CreateSolidBrush(bh ? RGB(45, 70, 115) : RGB(35, 35, 35));
                    FillRect(hDC, &rect, btnBrush);
                    DeleteObject(btnBrush);
                    auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                    FrameRect(hDC, &rect, btnBorder);
                    DeleteObject(btnBorder);
                }

                std::string full     = std::string(r.label) + " " + r.sliderValueText;
                RECT        labelRect = {gx + 10, iy, minusR.left - 4, iy + ITEM_H};
                SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
                DrawTextA(hDC, full.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hDC, cbxFont);
                SetTextColor(hDC, TAG_COLOR_WHITE);
                DrawTextA(hDC, "-", -1, &minusR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DrawTextA(hDC, "+", -1, &plusR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hDC, itemFont);

                auto minusId = std::format("MENU|{}", r.sliderMinusIdx);
                auto plusId  = std::format("MENU|{}", r.sliderPlusIdx);
                this->AddScreenObjectAuto(SCREEN_OBJECT_START_MENU_ITEM, minusId.c_str(),
                                          minusR, false, "");
                this->AddScreenObjectAuto(SCREEN_OBJECT_START_MENU_ITEM, plusId.c_str(),
                                          plusR, false, "");
            }
            else
            {
                // Action row — no checkbox, a framed button-style label spanning most of the width.
                RECT btnRect = {gx + 10, iy + 2, rowRect.right - 6, iy + ITEM_H - 2};
                auto btnBrush = CreateSolidBrush(r.disabled ? RGB(25, 25, 25)
                                                 : hov      ? RGB(45, 70, 115)
                                                            : RGB(35, 35, 35));
                FillRect(hDC, &btnRect, btnBrush);
                DeleteObject(btnBrush);
                auto btnBorder = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
                FrameRect(hDC, &btnRect, btnBorder);
                DeleteObject(btnBorder);

                SetTextColor(hDC, r.disabled ? RGB(70, 70, 70) : TAG_COLOR_WHITE);
                DrawTextA(hDC, r.label, -1, &btnRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                if (!r.disabled)
                {
                    auto objId = std::format("MENU|{}", r.menuIdx);
                    this->AddScreenObjectAuto(SCREEN_OBJECT_START_MENU_ITEM, objId.c_str(),
                                              btnRect, false, r.label);
                }
            }

            iy += ITEM_H;
        }
    };

    // Place the four groups in a 2×2 grid.
    const int col0X = wx + PAD;
    const int col1X = wx + PAD + GROUP_W + GROUP_GAP;
    const int row0Y = BODY_TOP;
    const int row1Y = BODY_TOP + GROUP_H + GROUP_GAP;
    drawGroup(groups[0], col0X, row0Y);
    drawGroup(groups[1], col1X, row0Y);
    drawGroup(groups[2], col0X, row1Y);
    drawGroup(groups[3], col1X, row1Y);

    SelectObject(hDC, prevFont);
    DeleteObject(hdrFont);
    DeleteObject(itemFont);
    DeleteObject(cbxFont);
}

void RadarScreen::CreateSettingsPopout(CFlowX_Settings* s)
{
    int w = SETTINGS_WIN_W;
    int h = SETTINGS_WIN_H;
    int x = s->GetSettingsPopoutX();
    int y = s->GetSettingsPopoutY();
    if (x == -1)
    {
        x = this->settingsWindowPos.x;
        y = this->settingsWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetSettingsPopoutPos(x, y);
    }
    this->settingsPopout = std::make_unique<PopoutWindow>(
        "FlowX Settings", x, y, w, h,
        [s](int nx, int ny)
        { s->SetSettingsPopoutPos(nx, ny); },
        [this]()
        { this->RequestRefresh(); },
        nullptr);
}
