/**
 * @file RadarScreen_Weather.cpp
 * @brief RadarScreen partial implementation: WX/ATIS window rendering and popout creation.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"

#include "RadarScreen.h"
#include "CFlowX_Settings.h"
#include "constants.h"
#include "helpers.h"

/// @brief Draws the WX/ATIS window: wind, QNH, and ATIS letter in a single horizontal row.
/// Values are highlighted yellow when changed; clicking the row acknowledges them.
void RadarScreen::DrawWeatherWindow(HDC hDC)
{
    auto* settingsWx = static_cast<CFlowX_Settings*>(this->GetPlugIn());
    if (!settingsWx->GetWeatherVisible())
    {
        return;
    }
    int fo = settingsWx->GetFontOffset();
    int op = settingsWx->GetBgOpacity();

    const int TITLE_H = 13;
    const int DATA_H  = 36;
    const int RVR_H   = 26; ///< Tighter row height for the RVR line
    const int WIN_PAD = 8;
    const int X_BTN   = 11; ///< Width and height of the title-bar close button

    static const WeatherRowCache emptyRow{"", "-", TAG_COLOR_DEFAULT_GRAY, "-", TAG_COLOR_DEFAULT_GRAY, "?", TAG_COLOR_DEFAULT_GRAY};
    const auto&                  r = this->weatherRowsCache.empty() ? emptyRow : this->weatherRowsCache[0];

    HFONT titleFont = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT dataFont  = CreateFontA(-20 - fo, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");

    // Measure content widths with the data font before drawing any background
    HFONT prev = (HFONT)SelectObject(hDC, dataFont);
    SIZE  spaceSize, qnhSize, atisSize, rvrSize;
    GetTextExtentPoint32A(hDC, " ", 1, &spaceSize);
    GetTextExtentPoint32A(hDC, r.qnh.c_str(), (int)r.qnh.size(), &qnhSize);
    GetTextExtentPoint32A(hDC, r.atis.c_str(), (int)r.atis.size(), &atisSize);
    GetTextExtentPoint32A(hDC, r.rvr.c_str(), (int)r.rvr.size(), &rvrSize);

    // Wind is drawn in two parts (direction + speed) with a half-space gap between them
    const int   halfGap   = spaceSize.cx / 2;
    std::string windDir   = (r.wind.size() >= 3) ? r.wind.substr(0, 3) : r.wind;
    std::string windSpeed = (r.wind.size() > 3) ? r.wind.substr(3) : "";
    SIZE        windDirSize, windSpeedSize;
    GetTextExtentPoint32A(hDC, windDir.c_str(), (int)windDir.size(), &windDirSize);
    GetTextExtentPoint32A(hDC, windSpeed.c_str(), (int)windSpeed.size(), &windSpeedSize);
    int windTotalW = windDirSize.cx + halfGap + windSpeedSize.cx;

    bool hasRVR = !r.rvr.empty();
    int  line1W = windTotalW + spaceSize.cx + qnhSize.cx + spaceSize.cx + atisSize.cx;
    int  WIN_W  = WIN_PAD + (line1W > rvrSize.cx ? line1W : rvrSize.cx) + WIN_PAD;
    int  WIN_H  = TITLE_H + DATA_H + (hasRVR ? RVR_H : 0) + WIN_PAD / 2;

    this->weatherWindowW = WIN_W;
    this->weatherWindowH = WIN_H;

    if (this->weatherWindowPos.x == -1)
    {
        RECT clip;
        GetClipBox(hDC, &clip);
        this->weatherWindowPos.x = clip.right - WIN_W - 20;
        this->weatherWindowPos.y = clip.top + 20;
    }

    int wx = this->weatherWindowPos.x;
    int wy = this->weatherWindowPos.y;

    RECT xRect                   = {wx + WIN_W - X_BTN - 1, wy + 1, wx + WIN_W - 1, wy + 1 + X_BTN};
    this->winCloseLastHoverType  = -1;
    this->winPopoutLastHoverType = -1;
    POINT cursorWx               = {-9999, -9999};
    if (this->isPopoutRender_)
        cursorWx = this->popoutHoverPoint_;
    else
    {
        HWND esHwnd = WindowFromDC(hDC);
        GetCursorPos(&cursorWx);
        if (esHwnd)
            ScreenToClient(esHwnd, &cursorWx);
    }
    bool xHovered   = PtInRect(&xRect, cursorWx) != 0;
    RECT popRectWx  = {xRect.left - X_BTN - 1, wy + 1, xRect.left - 1, wy + 1 + X_BTN};
    bool popHovered = PtInRect(&popRectWx, cursorWx) != 0;

    RECT winRect   = {wx, wy, wx + WIN_W, wy + WIN_H};
    RECT titleRect = {wx, wy, wx + WIN_W, wy + TITLE_H};

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
        FillRect(hDC, &popRectWx, popBrush);
        DeleteObject(popBrush);
    }

    auto borderBrush = CreateSolidBrush(TAG_COLOR_DEFAULT_GRAY);
    FrameRect(hDC, &winRect, borderBrush);
    DeleteObject(borderBrush);

    SetBkMode(hDC, TRANSPARENT);

    RECT dragRectWx = {wx, wy, popRectWx.left - 1, wy + TITLE_H};

    SelectObject(hDC, titleFont);
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "WX/ATIS", -1, &dragRectWx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Close and popout button text
    SetTextColor(hDC, TAG_COLOR_WHITE);
    DrawTextA(hDC, "x", -1, &xRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT popDrawRectWx = popRectWx;
    if (!this->isPopoutRender_)
        popDrawRectWx.top += 1;
    DrawTextA(hDC, this->isPopoutRender_ ? "v" : "^", -1, &popDrawRectWx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->AddScreenObjectAuto(SCREEN_OBJECT_WEATHER_WIN, "WXATIS", dragRectWx, true, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_CLOSE, "weather", xRect, false, "");
    this->AddScreenObjectAuto(SCREEN_OBJECT_WIN_POPOUT, "weather", popRectWx, false, "");

    SelectObject(hDC, dataFont);
    int textY = wy + TITLE_H + (DATA_H - spaceSize.cy) / 2;
    int x     = wx + WIN_PAD;

    auto drawField = [&](const std::string& text, COLORREF color, int w)
    {
        SetTextColor(hDC, color);
        TextOutA(hDC, x, textY, text.c_str(), (int)text.size());
        x += w + spaceSize.cx;
    };

    SetTextColor(hDC, r.windColor);
    TextOutA(hDC, x, textY, windDir.c_str(), (int)windDir.size());
    TextOutA(hDC, x + windDirSize.cx + halfGap, textY, windSpeed.c_str(), (int)windSpeed.size());
    x += windTotalW + spaceSize.cx;
    drawField(r.qnh, r.qnhColor, qnhSize.cx);
    drawField(r.atis, r.atisColor, atisSize.cx);

    if (hasRVR)
    {
        textY += RVR_H;
        x = wx + WIN_PAD;
        SetTextColor(hDC, r.rvrColor);
        TextOutA(hDC, x, textY, r.rvr.c_str(), (int)r.rvr.size());
    }

    RECT clickRect = {wx, wy + TITLE_H, wx + WIN_W, wy + WIN_H};
    this->AddScreenObjectAuto(SCREEN_OBJECT_WEATHER_ROW, r.icao.c_str(), clickRect, false, "");

    SelectObject(hDC, prev);
    DeleteObject(titleFont);
    DeleteObject(dataFont);
}

/// @brief Creates the Weather/ATIS popout window (fixed size — uses current window dimensions).
void RadarScreen::CreateWeatherPopout(CFlowX_Settings* s)
{
    int w = this->weatherWindowW;
    int h = this->weatherWindowH;
    int x = s->GetWeatherPopoutX();
    int y = s->GetWeatherPopoutY();
    if (x == -1)
    {
        x = this->weatherWindowPos.x;
        y = this->weatherWindowPos.y;
        if (this->esHwnd_)
        {
            POINT pt = {x, y};
            ClientToScreen(this->esHwnd_, &pt);
            x = pt.x;
            y = pt.y;
        }
        s->SetWeatherPopoutPos(x, y);
    }
    this->weatherPopout = std::make_unique<PopoutWindow>(
        "WX/ATIS", x, y, w, h,
        [s](int nx, int ny)
        { s->SetWeatherPopoutPos(nx, ny); });
}
