/**
 * @file PopoutWindow.h
 * @brief Generic always-on-top standalone window for popping out radar-screen panels.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// @brief Hit area registered during a draw pass; used to dispatch native mouse events on the popout thread.
struct HitArea
{
    int         objectType = 0;
    std::string objectId;
    RECT        rect     = {};
    bool        dragable = false;
};

/// @brief Mouse event queued by the popout window thread and dispatched on the EuroScope main thread.
struct PendingEvent
{
    enum class Type
    {
        Hover,
        LClick,
        RClick,
        DragMove,
        DragRelease
    };
    Type        type;
    int         objectType = 0;
    std::string objectId;
    POINT       pt     = {};
    RECT        area   = {};
    int         button = 0; ///< 0 = left (EuroScope BUTTON_LEFT), 1 = right (BUTTON_RIGHT)
};

/// @brief Generic always-on-top standalone window for popping out radar-screen panels.
///
/// Creates a borderless, topmost Win32 HWND on a dedicated thread with its own message loop.
/// Content is supplied as a GDI bitmap updated from the EuroScope main thread via UpdateContent().
/// The content bitmap includes the window's title bar, rendered by the same draw function used
/// for the in-screen version (with isPopoutRender_ set so the pop-out "^" becomes a pop-in "v").
/// Dragging, close, and pop-in are handled natively via WM_NCHITTEST and WM_LBUTTONDOWN.
///
/// @note The constructor blocks until the HWND is created on the window thread.
/// @note WS_EX_NOACTIVATE prevents stealing keyboard focus from EuroScope.
class PopoutWindow
{
  private:
    /// Width/height of title-bar buttons in pixels; must match the draw functions (X_BTN local var).
    static constexpr int X_BTN = 11;
    /// Height of the title bar inside the content area; used for HTCAPTION region and click detection.
    static constexpr int CONTENT_TITLE_H = 13;

    std::string      title;    ///< Window title string passed to CreateWindowEx (not drawn by this class)
    std::atomic<int> contentW; ///< Window width in pixels; atomic so RequestResize can read it on the popout thread
    std::atomic<int> contentH; ///< Window height in pixels; atomic so RequestResize can read it on the popout thread

    mutable std::mutex contentMutex;            ///< Guards contentBitmap / bmpW / bmpH
    HBITMAP            contentBitmap = nullptr; ///< Latest content bitmap; owned — deleted on next update
    int                bmpW          = 0;       ///< Width of contentBitmap in pixels
    int                bmpH          = 0;       ///< Height of contentBitmap in pixels

    std::atomic<bool>  closeRequested_ = false;   ///< Set on close click; polled by ES main thread
    std::atomic<bool>  popInRequested_ = false;   ///< Set on pop-in click; polled by ES main thread
    std::atomic<bool>  directDragging_ = false;   ///< True while onDirectDrag_ is actively handling a drag; suppresses RenderToPopout
    bool               hasPopInButton_ = true;    ///< When false, no pop-in button is rendered or hit-tested (popout-only windows)
    HICON              taskbarIcon_    = nullptr; ///< When set, window is created with WS_EX_APPWINDOW (visible in taskbar) and the icon is assigned via WM_SETICON; not owned
    std::atomic<bool>  topmost_        = true;    ///< Current always-on-top state; SetTopmost flips ex-style + Z order
    std::atomic<bool>  maximized_      = false;   ///< Current maximized state; SetMaximized stores savedRect_ before resizing
    RECT               savedRect_      = {};      ///< Window rect saved before maximize, used to restore
    mutable std::mutex savedRectMutex_;           ///< Guards savedRect_

    std::atomic<int>          cursorX_ = {-9999};           ///< Client-coords X of the mouse; -9999 when outside window
    std::atomic<int>          cursorY_ = {-9999};           ///< Client-coords Y of the mouse; -9999 when outside window
    mutable std::mutex        dragOverlayMutex_;            ///< Guards all dragOverlay* fields below
    HBITMAP                   dragOverlayBmp_    = nullptr; ///< Captured strip pixels; owned — deleted on replace/clear
    int                       dragOverlayW_      = 0;       ///< Width of dragOverlayBmp_ in pixels
    int                       dragOverlayH_      = 0;       ///< Height of dragOverlayBmp_ in pixels
    POINT                     dragOverlayCenter_ = {0, 0};  ///< Offset (px) from strip top-left to the point the cursor should track (callsign box centre)
    std::vector<RECT>         dragOverlayDropRects_;        ///< Drop-target rects (popout-local coords) to highlight when cursor is inside
    bool                      dragOverlayActive_ = false;   ///< True while a drag overlay should be composited on top of the cached bitmap
    bool                      dragging_          = false;   ///< True while a dragable hit area is being dragged; popout thread only
    HitArea                   dragTarget_        = {};      ///< Hit area currently being dragged; popout thread only
    POINT                     dragLastPt_        = {};      ///< Last mouse position during drag; used to compute delta; popout thread only
    mutable std::mutex        eventMutex_;                  ///< Guards pendingEvents_
    mutable std::mutex        hitAreasMutex_;               ///< Guards hitAreas_
    std::vector<HitArea>      hitAreas_;                    ///< Cleared before each draw; populated via AddScreenObject
    std::vector<PendingEvent> pendingEvents_;               ///< Events queued by the popout thread; consumed by RenderToPopout

    /// Callback signature for instant direct-drag handling on the popout thread.
    /// Receives the hit area, pixel delta, and current window size; returns the desired new {w, h}.
    /// Return {0, 0} to skip resize. Called on the popout thread — must be lock-free.
    using DirectDragFn = std::function<std::pair<int, int>(const HitArea&, POINT delta, int currentW, int currentH)>;

    /// @brief Callback invoked from WM_PAINT on the popout thread to draw the window content.
    /// Receives the window DC and current client size. When set, the PopoutWindow's WM_PAINT
    /// skips the cached-bitmap blit, drag-overlay compositing, and button hover-overdraw
    /// entirely — the paint fn is expected to own the full visual output.
    using ContentPaintFn = std::function<void(HDC hDC, int w, int h)>;

    mutable std::mutex contentPaintMutex_; ///< Guards contentPaintFn_
    ContentPaintFn     contentPaintFn_;    ///< When set, WM_PAINT calls this instead of blitting contentBitmap

    DirectDragFn                  onDirectDrag_;  ///< Instant resize callback; called on popout thread during drag; may be null
    std::function<void()>         onNeedsRefresh; ///< Reserved; may be called by the popout thread to hint at an ES repaint
    std::function<void(int, int)> onMoved;        ///< Called with new screen (x, y) when dragged; may be null

    HWND        hwnd = nullptr; ///< Created and owned by the window thread
    std::thread thread;         ///< Dedicated message-pump thread

    static ATOM       wndClassAtom;  ///< Registered exactly once across all instances
    static std::mutex wndClassMutex; ///< Guards one-time wndClassAtom registration

    void                    ThreadProc(int startX, int startY, std::atomic<bool>& readyFlag);
    LRESULT                 HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    /// @brief Blits the current content bitmap to the window DC.
    void Paint(HDC hDC);

    /// @brief Returns the close [x] button rect in client coordinates (matches draw function layout).
    [[nodiscard]] RECT GetCloseRect() const;

    /// @brief Returns the pop-in [v] button rect in client coordinates (matches draw function layout).
    [[nodiscard]] RECT GetPopInRect() const;

  public:
    PopoutWindow(std::string title, int startX, int startY, int cW, int cH,
                 std::function<void(int, int)> onMoved,
                 std::function<void()>         onNeedsRefresh = nullptr,
                 DirectDragFn                  onDirectDrag   = nullptr,
                 bool                          hasPopInButton = true,
                 HICON                         taskbarIcon    = nullptr);
    ~PopoutWindow();

    PopoutWindow(const PopoutWindow&)            = delete;
    PopoutWindow& operator=(const PopoutWindow&) = delete;
    PopoutWindow(PopoutWindow&&)                 = delete;
    PopoutWindow& operator=(PopoutWindow&&)      = delete;

    /// @brief Thread-safe: replaces the displayed content bitmap. Takes ownership of @p bmp.
    void UpdateContent(HBITMAP bmp, int w, int h);

    /// @brief Thread-safe: installs a content paint callback invoked from WM_PAINT on the
    /// popout thread. When set, the cached-bitmap blit path is bypassed and @p fn is expected
    /// to draw the entire window content (including any title-bar buttons and drag overlays).
    void SetContentPaintFn(ContentPaintFn fn);

    /// @brief Enables a drag-overlay composited on top of the cached bitmap in WM_PAINT.
    /// Takes ownership of @p stripBmp (deleted on replace / ClearDragOverlay / destructor).
    /// @param centerOffset Pixel offset from strip top-left to the point that should follow the cursor.
    /// @param dropRects    Rects (popout-local coords) to highlight when cursor is inside them.
    void SetDragOverlay(HBITMAP stripBmp, int w, int h, POINT centerOffset, std::vector<RECT> dropRects);

    /// @brief Clears any active drag overlay and frees the held bitmap.
    void ClearDragOverlay();

    /// @brief Registers a hit area for the current frame; used by RenderToPopout via AddScreenObjectAuto.
    void AddScreenObject(int objectType, const char* objectId, RECT rect, bool dragable, const char* tooltip);

    /// @brief Clears all hit areas; called by RenderToPopout before each draw.
    void ClearScreenObjects();

    /// @brief Returns the current cursor position in popout-window client coordinates; {-9999,-9999} when outside.
    [[nodiscard]] POINT GetCursorPosition() const;

    /// @brief Drains and returns all pending events queued by the window thread; called on the EuroScope main thread.
    [[nodiscard]] std::vector<PendingEvent> ConsumeEvents();

    /// @brief Resizes the popout HWND and updates contentW/H if the bitmap dimensions changed.
    void ResizeIfNeeded(int w, int h);

    /// @brief Resizes the window immediately; safe to call from the popout thread (e.g. from onDirectDrag_).
    void RequestResize(int w, int h);

    /// @brief Returns the current content width; safe to call from any thread.
    [[nodiscard]] int GetContentW() const
    {
        return this->contentW.load();
    }

    /// @brief Returns the current content height; safe to call from any thread.
    [[nodiscard]] int GetContentH() const
    {
        return this->contentH.load();
    }

    /// @brief Requests an immediate repaint of the popout window; equivalent to EuroScope RequestRefresh().
    /// Safe to call from any thread.
    void RequestRepaint()
    {
        HWND hwndCopy = this->hwnd;
        if (hwndCopy)
            InvalidateRect(hwndCopy, nullptr, FALSE);
    }

    /// @brief Toggles always-on-top. When false, the window loses WS_EX_NOACTIVATE/TOOLWINDOW and
    ///        gains WS_EX_APPWINDOW so it appears in the taskbar and can be activated normally.
    /// Safe to call from any thread.
    void SetTopmost(bool v);

    /// @brief Toggles maximized (work-area-fill) mode. Saves the current rect on maximize and
    ///        restores it on un-maximize. Also updates contentW/H so the content bitmap resizes.
    /// Safe to call from any thread.
    void SetMaximized(bool v);

    [[nodiscard]] bool IsTopmost() const
    {
        return this->topmost_.load();
    }
    [[nodiscard]] bool IsMaximized() const
    {
        return this->maximized_.load();
    }

    [[nodiscard]] bool IsCloseRequested() const
    {
        return this->closeRequested_.load();
    }
    [[nodiscard]] bool IsPopInRequested() const
    {
        return this->popInRequested_.load();
    }
    /// @brief True while onDirectDrag_ is actively handling a drag; RenderToPopout should skip rendering.
    [[nodiscard]] bool IsDirectDragging() const
    {
        return this->directDragging_.load();
    }
};
