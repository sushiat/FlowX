/**
 * @file PopoutWindow.cpp
 * @brief Generic always-on-top standalone window for popping out radar-screen panels.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "PopoutWindow.h"

#include <windowsx.h>

// ── Static member definitions ───────────────────────────────────────────────────
ATOM       PopoutWindow::wndClassAtom = 0;
std::mutex PopoutWindow::wndClassMutex;

/// Custom message posted to the window thread to trigger InvalidateRect after a content update.
static constexpr UINT WM_CONTENT_UPDATED = WM_USER + 1;

static constexpr char WNDCLASS_NAME[] = "FlowXPopoutWindow";

// ── Constructor / Destructor ────────────────────────────────────────────────────

PopoutWindow::PopoutWindow(std::string title_, int startX, int startY, int cW, int cH,
                           std::function<void(int, int)> onMoved_,
                           std::function<void()>         onNeedsRefresh_,
                           DirectDragFn                  onDirectDrag_)
    : title(std::move(title_)), contentW(cW), contentH(cH),
      onDirectDrag_(std::move(onDirectDrag_)), onNeedsRefresh(std::move(onNeedsRefresh_)),
      onMoved(std::move(onMoved_))
{
    std::atomic<bool> readyFlag = false;
    this->thread                = std::thread(
        [this, startX, startY, &readyFlag]
        {
            this->ThreadProc(startX, startY, readyFlag);
        });
    while (!readyFlag.load())
    {
        Sleep(1);
    }
}

PopoutWindow::~PopoutWindow()
{
    HWND h = this->hwnd;
    if (h)
    {
        PostMessage(h, WM_CLOSE, 0, 0);
    }
    if (this->thread.joinable())
    {
        this->thread.join();
    }
    std::lock_guard lock(this->contentMutex);
    if (this->contentBitmap)
    {
        DeleteObject(this->contentBitmap);
        this->contentBitmap = nullptr;
    }
}

// ── Hit-area / event methods ────────────────────────────────────────────────────

void PopoutWindow::AddScreenObject(int objectType, const char* objectId, RECT rect, bool dragable,
                                   const char* /*tooltip*/)
{
    std::lock_guard lock(this->hitAreasMutex_);
    this->hitAreas_.push_back({objectType, objectId ? objectId : "", rect, dragable});
}

void PopoutWindow::ClearScreenObjects()
{
    std::lock_guard lock(this->hitAreasMutex_);
    this->hitAreas_.clear();
}

POINT PopoutWindow::GetCursorPosition() const
{
    return {this->cursorX_.load(), this->cursorY_.load()};
}

std::vector<PendingEvent> PopoutWindow::ConsumeEvents()
{
    std::vector<PendingEvent> out;
    std::lock_guard           lock(this->eventMutex_);
    out.swap(this->pendingEvents_);
    return out;
}

void PopoutWindow::ResizeIfNeeded(int w, int h)
{
    if (w == this->contentW.load() && h == this->contentH.load())
        return;
    this->contentW.store(w);
    this->contentH.store(h);
    HWND hwndCopy = this->hwnd;
    if (hwndCopy)
        SetWindowPos(hwndCopy, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PopoutWindow::RequestResize(int w, int h)
{
    this->contentW.store(w);
    this->contentH.store(h);
    HWND hwndCopy = this->hwnd;
    if (hwndCopy)
        SetWindowPos(hwndCopy, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ── UpdateContent ───────────────────────────────────────────────────────────────

void PopoutWindow::UpdateContent(HBITMAP bmp, int w, int h)
{
    {
        std::lock_guard lock(this->contentMutex);
        if (this->contentBitmap)
        {
            DeleteObject(this->contentBitmap);
        }
        this->contentBitmap = bmp;
        this->bmpW          = w;
        this->bmpH          = h;
    }
    HWND hwndCopy = this->hwnd;
    if (hwndCopy)
    {
        PostMessage(hwndCopy, WM_CONTENT_UPDATED, 0, 0);
    }
}

// ── Window thread ───────────────────────────────────────────────────────────────

void PopoutWindow::ThreadProc(int startX, int startY, std::atomic<bool>& readyFlag)
{
    {
        std::lock_guard lock(wndClassMutex);
        if (!wndClassAtom)
        {
            WNDCLASSEXA wc   = {};
            wc.cbSize        = sizeof(wc);
            wc.style         = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc   = PopoutWindow::WndProc;
            wc.hInstance     = GetModuleHandleA(nullptr);
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.lpszClassName = WNDCLASS_NAME;
            wndClassAtom     = RegisterClassExA(&wc);
        }
    }

    // Window size equals content size — the content bitmap includes the title bar.
    this->hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WNDCLASS_NAME,
                                 this->title.c_str(), WS_POPUP | WS_VISIBLE, startX, startY,
                                 this->contentW, this->contentH, nullptr, nullptr,
                                 GetModuleHandleA(nullptr), this);

    readyFlag.store(true);

    if (!this->hwnd)
    {
        return;
    }

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

// ── Static WndProc ──────────────────────────────────────────────────────────────

LRESULT CALLBACK PopoutWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PopoutWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        self     = static_cast<PopoutWindow*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<PopoutWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->HandleMessage(hwnd, msg, wp, lp);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ── Geometry helpers ─────────────────────────────────────────────────────────────

/// Button positions mirror the draw functions exactly (xRect / popRect at wx=0, wy=0).
RECT PopoutWindow::GetCloseRect() const
{
    return {this->contentW - X_BTN - 1, 1, this->contentW - 1, 1 + X_BTN};
}

RECT PopoutWindow::GetPopInRect() const
{
    RECT close = GetCloseRect();
    return {close.left - X_BTN - 1, 1, close.left - 1, 1 + X_BTN};
}

// ── Paint ────────────────────────────────────────────────────────────────────────

/// Blits the content bitmap, then overdraws close and pop-in button hover states using the
/// live atomic cursor position. This decouples button hover feedback from the EuroScope
/// refresh cycle so highlights respond immediately to mouse movement.
void PopoutWindow::Paint(HDC hDC)
{
    {
        std::lock_guard lock(this->contentMutex);
        if (!this->contentBitmap || this->bmpW <= 0 || this->bmpH <= 0)
            return;
        HDC     memDC  = CreateCompatibleDC(hDC);
        HGDIOBJ oldBmp = SelectObject(memDC, this->contentBitmap);
        int     dstW   = this->contentW.load();
        int     dstH   = this->contentH.load();
        if (dstW == this->bmpW && dstH == this->bmpH)
        {
            BitBlt(hDC, 0, 0, dstW, dstH, memDC, 0, 0, SRCCOPY);
        }
        else
        {
            // Window size differs from bitmap (live resize in progress) — stretch to fill.
            SetStretchBltMode(hDC, COLORONCOLOR);
            StretchBlt(hDC, 0, 0, dstW, dstH, memDC, 0, 0, this->bmpW, this->bmpH, SRCCOPY);
        }
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }

    // Overdraw button hover states using the live cursor so they update at WM_PAINT speed.
    POINT cursor    = {this->cursorX_.load(), this->cursorY_.load()};
    RECT  closeRect = GetCloseRect();
    RECT  popInRect = GetPopInRect();
    HFONT btnFont   = CreateFontA(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT prevFont  = static_cast<HFONT>(SelectObject(hDC, btnFont));
    SetBkMode(hDC, TRANSPARENT);
    SetTextColor(hDC, RGB(255, 255, 255));
    if (PtInRect(&closeRect, cursor))
    {
        auto b = CreateSolidBrush(RGB(180, 40, 40));
        FillRect(hDC, &closeRect, b);
        DeleteObject(b);
        DrawTextA(hDC, "x", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (PtInRect(&popInRect, cursor))
    {
        auto b = CreateSolidBrush(RGB(40, 100, 160));
        FillRect(hDC, &popInRect, b);
        DeleteObject(b);
        DrawTextA(hDC, "v", -1, &popInRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hDC, prevFont);
    DeleteObject(btnFont);
}

// ── Instance WndProc ─────────────────────────────────────────────────────────────

LRESULT PopoutWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        // Button areas are HTCLIENT so WM_LBUTTONDOWN fires; title bar area is HTCAPTION for free OS drag.
        RECT closeRect = GetCloseRect();
        RECT popInRect = GetPopInRect();
        if (PtInRect(&closeRect, pt) || PtInRect(&popInRect, pt))
            return HTCLIENT;
        if (pt.y >= 0 && pt.y < CONTENT_TITLE_H)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOVING:
    {
        auto* r = reinterpret_cast<RECT*>(lp);
        if (this->onMoved)
        {
            this->onMoved(r->left, r->top);
        }
        return TRUE;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt        = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT  closeRect = GetCloseRect();
        RECT  popInRect = GetPopInRect();
        if (PtInRect(&closeRect, pt))
        {
            this->closeRequested_.store(true);
            DestroyWindow(hwnd);
        }
        else if (PtInRect(&popInRect, pt))
        {
            this->popInRequested_.store(true);
            DestroyWindow(hwnd);
        }
        else
        {
            HitArea hit;
            bool    found = false;
            {
                std::lock_guard lock(this->hitAreasMutex_);
                for (const auto& ha : this->hitAreas_)
                {
                    if (PtInRect(&ha.rect, pt))
                    {
                        hit   = ha;
                        found = true;
                        break;
                    }
                }
            }
            if (found)
            {
                if (hit.dragable)
                {
                    this->dragging_   = true;
                    this->dragTarget_ = hit;
                    this->dragLastPt_ = pt;
                    SetCapture(hwnd);
                }
                PendingEvent    ev{PendingEvent::Type::LClick,
                                   hit.objectType, hit.objectId, pt, hit.rect, 0};
                std::lock_guard lock(this->eventMutex_);
                this->pendingEvents_.push_back(std::move(ev));
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (this->dragging_)
        {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ReleaseCapture();
            this->directDragging_.store(false); // allow RenderToPopout to resume
            PendingEvent ev{PendingEvent::Type::DragRelease,
                            this->dragTarget_.objectType, this->dragTarget_.objectId,
                            pt, this->dragTarget_.rect, 0};
            {
                std::lock_guard lock(this->eventMutex_);
                this->pendingEvents_.push_back(std::move(ev));
            }
            this->dragging_   = false;
            this->dragTarget_ = {};
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        POINT   pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        HitArea hit;
        bool    found = false;
        {
            std::lock_guard lock(this->hitAreasMutex_);
            for (const auto& ha : this->hitAreas_)
            {
                if (PtInRect(&ha.rect, pt))
                {
                    hit   = ha;
                    found = true;
                    break;
                }
            }
        }
        if (found)
        {
            PendingEvent    ev{PendingEvent::Type::RClick,
                               hit.objectType, hit.objectId, pt, hit.rect, 1};
            std::lock_guard lock(this->eventMutex_);
            this->pendingEvents_.push_back(std::move(ev));
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        this->cursorX_.store(pt.x);
        this->cursorY_.store(pt.y);
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, nullptr, FALSE); // repaint immediately for button hover feedback
        if (this->dragging_)
        {
            POINT delta          = {pt.x - this->dragLastPt_.x, pt.y - this->dragLastPt_.y};
            this->dragLastPt_    = pt;
            bool directlyHandled = false;
            if (this->onDirectDrag_)
            {
                auto [newW, newH] = this->onDirectDrag_(this->dragTarget_, delta,
                                                        this->contentW.load(), this->contentH.load());
                if (newW > 0 && newH > 0)
                {
                    this->RequestResize(newW, newH);
                    this->directDragging_.store(true); // suppress RenderToPopout until mouse-up
                    directlyHandled = true;            // suppress DragMove — ES catches up on DragRelease
                }
            }
            if (!directlyHandled)
            {
                PendingEvent    ev{PendingEvent::Type::DragMove,
                                   this->dragTarget_.objectType, this->dragTarget_.objectId,
                                   pt, this->dragTarget_.rect, 0};
                std::lock_guard lock(this->eventMutex_);
                this->pendingEvents_.push_back(std::move(ev));
            }
        }
        else
        {
            HitArea hit;
            bool    found = false;
            {
                std::lock_guard lock(this->hitAreasMutex_);
                for (const auto& ha : this->hitAreas_)
                {
                    if (PtInRect(&ha.rect, pt))
                    {
                        hit   = ha;
                        found = true;
                        break;
                    }
                }
            }
            if (found)
            {
                PendingEvent    ev{PendingEvent::Type::Hover,
                                   hit.objectType, hit.objectId, pt, hit.rect, 0};
                std::lock_guard lock(this->eventMutex_);
                this->pendingEvents_.push_back(std::move(ev));
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        this->cursorX_.store(-9999);
        this->cursorY_.store(-9999);
        InvalidateRect(hwnd, nullptr, FALSE); // clear hover highlight immediately
        return 0;

    case WM_CONTENT_UPDATED:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC         hDC = BeginPaint(hwnd, &ps);
        this->Paint(hDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        this->hwnd = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}
