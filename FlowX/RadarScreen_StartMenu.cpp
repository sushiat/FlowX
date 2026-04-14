/**
 * @file RadarScreen_StartMenu.cpp
 * @brief RadarScreen partial implementation: NAP reminder, Start button, and Start menu rendering.
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
#include <string>
#include <mmsystem.h>
#include "constants.h"
#include "helpers.h"

/// @brief Draws the NAP reminder as a custom floating window with a draggable body and an ACK button.
/// OnOverScreenObject calls RequestRefresh() on every mouse-movement event so this function runs
/// frequently. GetCursorPos() then reflects the live cursor position accurately at each draw.
void RadarScreen::DrawNapReminder(HDC hDC)
{
    if (!this->napReminderActive)
    {
        return;
    }

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
        this->napWindowPos.x = clip.right / 2 - WIN_W / 2;
        this->napWindowPos.y = clip.bottom / 2 - WIN_H / 2;
    }

    int wx = this->napWindowPos.x;
    int wy = this->napWindowPos.y;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};
    RECT bodyRect  = {wx + PAD, wy + TITLE_H, wx + WIN_W - PAD, wy + TITLE_H + MSG_H};
    RECT btnRect   = {wx + PAD, wy + TITLE_H + MSG_H, wx + WIN_W - PAD, wy + TITLE_H + MSG_H + BTN_H - 4};
    // Drag zone covers title+body (does NOT overlap btnRect so ES fires separate OnOverScreenObject per area)
    RECT dragRect = {wx, wy, wx + WIN_W, wy + TITLE_H + MSG_H};

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
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursorPt;
    GetCursorPos(&cursorPt);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursorPt);
    }
    // During blink napAckPressed drives the colour; GetCursorPos hover is irrelevant then
    bool ackHovered = (this->napAckClickTick == 0) && PtInRect(&btnRect, cursorPt) != 0;

    // Background
    auto bgBrush    = CreateSolidBrush(RGB(15, 15, 15));
    auto titleBrush = CreateSolidBrush(RGB(30, 55, 95));
    FillRect(hDC, &winRect, bgBrush);
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
    HFONT prev      = (HFONT)SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "Night SID Reminder", -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    AddScreenObject(SCREEN_OBJECT_NAP_WIN, "NAPWIN", dragRect, true, "");

    // Message body
    HFONT msgFont = CreateFontA(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev          = (HFONT)SelectObject(hDC, msgFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    std::string msg = "NAP procedure for " + this->napReminderAirport + "?";
    DrawTextA(hDC, msg.c_str(), -1, &bodyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hDC, prev);
    DeleteObject(msgFont);

    // ACK button — three visual states: normal / hover / pressed
    COLORREF btnBg     = this->napAckPressed ? RGB(90, 175, 90)
                         : ackHovered        ? RGB(60, 130, 60)
                                             : RGB(35, 90, 35);
    COLORREF btnBorder = this->napAckPressed ? RGB(200, 255, 200)
                         : ackHovered        ? RGB(120, 200, 120)
                                             : RGB(80, 150, 80);
    auto     btnBrush  = CreateSolidBrush(btnBg);
    FillRect(hDC, &btnRect, btnBrush);
    DeleteObject(btnBrush);

    auto btnBorderBrush = CreateSolidBrush(btnBorder);
    FrameRect(hDC, &btnRect, btnBorderBrush);
    DeleteObject(btnBorderBrush);

    HFONT btnFont = CreateFontA(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    prev          = (HFONT)SelectObject(hDC, btnFont);
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
    int  bottom   = chatOpen ? chat.top : clip.bottom + 5; // +5: EuroScope clip-box excludes the 5 px status-bar gap when chat is closed
    int  bx       = clip.right - BTN_W;
    int  by       = bottom - BTN_H;

    RECT btnRect = {bx, by, bx + BTN_W, by + BTN_H};

    // Reset hover tracking each frame so every new mouse-enter counts as a fresh transition.
    this->startBtnLastHoverType = -1;

    // Read live cursor position for hover detection; RequestRefresh() from OnOverScreenObject keeps this current.
    HWND  hwnd = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursor);
    }
    bool hovered = PtInRect(&btnRect, cursor) != 0;

    // Background — three visual states: normal / hover / pressed
    COLORREF bgColor     = this->startBtnPressed ? RGB(70, 110, 170)
                           : hovered             ? RGB(50, 85, 135)
                                                 : RGB(30, 55, 95);
    COLORREF borderColor = this->startBtnPressed ? RGB(180, 210, 255)
                           : hovered             ? RGB(120, 160, 220)
                                                 : RGB(80, 120, 180);

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
    if (!this->startMenuOpen)
    {
        return;
    }

    const int BTN_H     = 20; ///< Must match DrawStartButton::BTN_H (fixed)
    auto*     base      = static_cast<CFlowX_Base*>(this->GetPlugIn());
    auto*     settings  = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    const int fo        = settings->GetFontOffset();
    const int HEADER_H  = 16 + fo;              ///< Height of a section header row
    const int ITEM_H    = 20 + fo;              ///< Height of a clickable item row
    const int GAP_H     = 10;                   ///< Spacer height inserted above each section header after the first
    const int OUTER_PAD = 6;                    ///< Margin from the outer border to all content on every side
    const int CBX_S     = 9 + fo;               ///< Checkbox square size in pixels
    const int CBX_GAP   = 4;                    ///< Gap between checkbox right edge and item label
    const int MENU_W    = 180 * (14 + fo) / 14; ///< Menu width scaled with item font; right-aligned with the button

    struct MenuRow
    {
        bool        isHeader;
        const char* label;
        bool        hasCheckbox;
        bool        checked;
        int         itemIdx;
        bool        hasFontButtons = false;
        bool        hasBgButtons   = false;
        bool        disabled       = false;
    };

    const bool osmBusy = settings->IsOsmBusy();

    MenuRow rows[] = {
        {true, "Windows", false, false, -1},
        {false, "Approach Estimate", true, settings->GetApproachEstVisible(), 16},
        {false, "DEP/H", true, settings->GetDepRateVisible(), 4},
        {false, "TWR Outbound", true, settings->GetTwrOutboundVisible(), 5},
        {false, "TWR Inbound", true, settings->GetTwrInboundVisible(), 6},
        {false, "WX/ATIS", true, settings->GetWeatherVisible(), 7},
        {false, "DIFLIS", true, settings->GetDiflisVisible(), 32},
        {true, "Commands", false, false, -1},
        {false, "Redo CLR flags", false, false, 0},
        {false, "Dismiss QNH", false, false, 12},
        {false, "Save positions", false, false, 1},
        {true, "Assists", false, false, -1},
        {false, "Auto-Restore FPLN", true, settings->GetAutoRestore(), 3},
        {false, "Auto PARK", true, settings->GetAutoParked(), 15},
        {false, "Auto-Clear Scratch", true, settings->GetAutoScratchpadClear(), 18},
        {false, "HP auto-scratch", true, settings->GetHpAutoScratch(), 28},
        {true, "Notifications", false, false, -1},
        {false, "Airborne", true, settings->GetSoundAirborne(), 19},
        {false, "GND Transfer", true, settings->GetSoundGndTransfer(), 20},
        {false, "Ready T/O", true, settings->GetSoundReadyTakeoff(), 21},
        {false, "No Route", true, settings->GetSoundNoRoute(), 30},
        {false, "Taxi Conflict", true, settings->GetSoundTaxiConflict(), 27},
        {true, "Options", false, false, -1},
        {false, "Debug mode", true, base->GetDebug(), 2},
        {false, "Update check", true, settings->GetUpdateCheck(), 13},
        {false, "Flash messages", true, settings->GetFlashOnMessage(), 14},
        {false, "Appr Est Colors", true, settings->GetApprEstColors(), 17},
        {false, "Fonts", false, false, -1, true},
        {false, "BG opacity", false, false, -1, false, true},
        {true, "TAXI", false, false, -1},
        {false, "Update TAXI info", false, false, 22, false, false, osmBusy},
        {false, "Clear TAXI routes", false, false, 26},
        {false, "Show TAXI network", true, this->showTaxiOverlay, 23},
        {false, "Show TAXI labels", true, this->showTaxiLabels, 24},
        {false, "Show TAXI routes", true, this->showTaxiRoutes, 25},
        {false, "Show TAXI graph", true, this->showTaxiGraph, 29},
        {false, "Log TAXI tests", true, this->logTaxiTests, 31},
    };
    const int NUM_ROWS = (int)(sizeof(rows) / sizeof(rows[0]));

    // Compute total menu height: outer padding at bottom only (top header sits flush with the border), gaps before non-first headers, row heights.
    int MENU_H = OUTER_PAD;
    for (auto [i, row] : std::views::enumerate(rows))
    {
        MENU_H += (row.isHeader && i > 0) ? GAP_H + HEADER_H : row.isHeader ? HEADER_H
                                                                            : ITEM_H;
    }

    // Mirror DrawStartButton's anchor logic exactly.
    RECT clip;
    GetClipBox(hDC, &clip);
    RECT chat     = GetChatArea();
    bool chatOpen = (chat.bottom > chat.top);
    int  bottom   = chatOpen ? chat.top : clip.bottom + 5; // +5: matches DrawStartButton's status-bar gap offset
    int  mx       = clip.right - MENU_W;
    int  my       = bottom - BTN_H - MENU_H;

    // Hover detection — reset each frame so every enter counts as a fresh transition.
    this->startMenuLastHoverType = -1;
    HWND  hwnd                   = WindowFromDC(hDC);
    POINT cursor;
    GetCursorPos(&cursor);
    if (hwnd)
    {
        ScreenToClient(hwnd, &cursor);
    }

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
    RECT menuRect = {mx, my, mx + MENU_W, my + MENU_H};
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
    HFONT prev       = (HFONT)SelectObject(hDC, itemFont);

    int iy = my;
    for (auto [i, row] : std::views::enumerate(rows))
    {
        int  rh      = row.isHeader ? HEADER_H : ITEM_H;
        RECT rowRect = {mx, iy, mx + MENU_W, iy + rh}; // rowRect for headers is updated after gap advance below

        if (row.isHeader)
        {
            // Gap spacer above every section header except the first.
            if (i > 0)
            {
                iy += GAP_H;
            }

            // Section header — blue title-bar background, inset by 1 so it doesn't paint over the border.
            rowRect       = {mx + 1, iy, mx + MENU_W - 1, iy + HEADER_H};
            auto hdrBrush = CreateSolidBrush(RGB(30, 55, 95));
            FillRect(hDC, &rowRect, hdrBrush);
            DeleteObject(hdrBrush);

            SelectObject(hDC, headerFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            RECT textRect = {mx + OUTER_PAD, iy, mx + MENU_W - OUTER_PAD, iy + HEADER_H};
            DrawTextA(hDC, row.label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (row.hasFontButtons)
        {
            // Fonts row: label + current offset value on the left, [−] [+] buttons on the right.
            const int   BTN_W  = 14 + fo;
            int         fo     = settings->GetFontOffset();
            std::string valStr = (fo > 0) ? std::format("+{}", fo) : std::format("{}", fo);

            RECT minusRect = {mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD - BTN_W - 2, iy + (ITEM_H + BTN_W) / 2};
            RECT plusRect  = {mx + MENU_W - OUTER_PAD - BTN_W, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD, iy + (ITEM_H + BTN_W) / 2};
            bool minusHov  = PtInRect(&minusRect, cursor) != 0;
            bool plusHov   = PtInRect(&plusRect, cursor) != 0;

            for (auto [rect, hov] : {std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov}})
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
            RECT        labelRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H};
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|8", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|9", plusRect, false, "");
        }
        else if (row.hasBgButtons)
        {
            // BG opacity row: label + current value on the left, [−] [+] buttons on the right.
            const int   BTN_W  = 14 + fo;
            int         bgOp   = settings->GetBgOpacity();
            std::string valStr = std::format("{}%", bgOp);

            RECT minusRect = {mx + MENU_W - OUTER_PAD - BTN_W * 2 - 2, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD - BTN_W - 2, iy + (ITEM_H + BTN_W) / 2};
            RECT plusRect  = {mx + MENU_W - OUTER_PAD - BTN_W, iy + (ITEM_H - BTN_W) / 2,
                              mx + MENU_W - OUTER_PAD, iy + (ITEM_H + BTN_W) / 2};
            bool minusHov  = PtInRect(&minusRect, cursor) != 0;
            bool plusHov   = PtInRect(&plusRect, cursor) != 0;

            for (auto [rect, hov] : {std::pair{minusRect, minusHov}, std::pair{plusRect, plusHov}})
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
            RECT        labelRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, minusRect.left - 4, iy + ITEM_H};
            SetTextColor(hDC, TAG_COLOR_LIST_GRAY);
            DrawTextA(hDC, fullLabel.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hDC, cbxFont);
            SetTextColor(hDC, TAG_COLOR_WHITE);
            DrawTextA(hDC, "-", -1, &minusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(hDC, "+", -1, &plusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|10", minusRect, false, "");
            AddScreenObject(SCREEN_OBJECT_START_MENU_ITEM, "MENU|11", plusRect, false, "");
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
                RECT boxRect = {cx, cy, cx + CBX_S, cy + CBX_S};

                auto boxBrush = CreateSolidBrush(row.checked ? RGB(30, 55, 95) : RGB(25, 25, 25));
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
            RECT textRect = {mx + OUTER_PAD + CBX_S + CBX_GAP, iy, mx + MENU_W - OUTER_PAD, iy + ITEM_H};
            SetTextColor(hDC, row.disabled ? RGB(70, 70, 70)
                              : rowHovered ? TAG_COLOR_WHITE
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
