#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <wincodec.h>
#include <wchar.h>

#include "overlay.h"

enum { LABEL_CAPACITY = 256, PLACEMENT_POLL_MS = 50 };

static const WCHAR window_class[] = L"Recompia.BlockLens.Tooltip";
static SRWLOCK worker_lock = SRWLOCK_INIT;
static SRWLOCK label_lock = SRWLOCK_INIT;
typedef struct Content {
    WCHAR label[LABEL_CAPACITY];
    WCHAR details[96];
    unsigned item_id;
    OverlayPosition position;
    int progress;
} Content;
static Content pending;
static struct {
    HMODULE module;
    HANDLE thread;
    HANDLE stop;
    HANDLE changed;
} worker;

typedef struct {
    HWND window;
    HWND game;
    HFONT font;
    HFONT details_font;
    UINT dpi;
    int text_width;
    Content content;
    IWICImagingFactory *imaging;
    HBITMAP icon;
    UINT icon_width, icon_height;
} Tooltip;

static int scaled(const Tooltip *tip, int pixels)
{
    return MulDiv(pixels, (int)tip->dpi, 96);
}

static void load_icon(Tooltip *tip, unsigned item_id)
{
    if (tip->icon) DeleteObject(tip->icon);
    tip->icon = NULL;
    if (!tip->imaging || !item_id) return;

    WCHAR path[4096];
    const DWORD len = GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    if (!len || len >= ARRAYSIZE(path)) return;
    WCHAR *filename = wcsrchr(path, L'\\');
    if (!filename) return;
    ++filename;
    if (swprintf(filename, ARRAYSIZE(path) - (size_t)(filename - path),
                 L"pokohex_data\\images\\items\\item_%04u.png", item_id) < 0)
        return;
    IWICBitmapDecoder *decoder;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(tip->imaging, path, NULL, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnDemand, &decoder)))
        return;
    IWICBitmapFrameDecode *frame;
    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame))) {
        IWICFormatConverter *converter;
        if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(tip->imaging, &converter))) {
            UINT width = 0, height = 0;
            if (SUCCEEDED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame,
                         &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0,
                         WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(IWICFormatConverter_GetSize(converter, &width, &height)) &&
                width && height && width <= 1024 && height <= 1024) {
                BITMAPINFO info = {0};
                info.bmiHeader.biSize = sizeof info.bmiHeader;
                info.bmiHeader.biWidth = (LONG)width;
                info.bmiHeader.biHeight = -(LONG)height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                void *pixels = NULL;
                HBITMAP bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
                if (bitmap && SUCCEEDED(IWICFormatConverter_CopyPixels(
                                  converter, NULL, 4 * width, 4 * width * height, pixels))) {
                    tip->icon = bitmap;
                    tip->icon_width = width;
                    tip->icon_height = height;
                } else if (bitmap) {
                    DeleteObject(bitmap);
                }
            }
            IWICFormatConverter_Release(converter);
        }
        IWICBitmapFrameDecode_Release(frame);
    }
    IWICBitmapDecoder_Release(decoder);
}

static void paint_icon(HDC dc, const Tooltip *tip, int x, int height)
{
    int side = scaled(tip, 30);
    HDC source = CreateCompatibleDC(dc);
    if (!source) return;
    HGDIOBJ old_bitmap = SelectObject(source, tip->icon);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, (height - side) / 2, side, side, source, 0, 0,
               (int)tip->icon_width, (int)tip->icon_height, blend);
    SelectObject(source, old_bitmap);
    DeleteDC(source);
}

static void paint_tooltip(HWND window, const Tooltip *tip)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT rect;
    GetClientRect(window, &rect);
    HBRUSH background = CreateSolidBrush(RGB(24, 32, 41));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(89, 108, 125));
    HGDIOBJ old_brush = SelectObject(dc, background);
    HGDIOBJ old_pen = SelectObject(dc, border);
    const int corner = scaled(tip, 16);
    FillRect(dc, &rect, background);
    RoundRect(dc, 0, 0, rect.right, rect.bottom, corner, corner);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(border);
    DeleteObject(background);

    HGDIOBJ old_font = SelectObject(dc, tip->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(250, 250, 247));
    rect.left += scaled(tip, 14);
    rect.right -= scaled(tip, 14);
    if (tip->icon) {
        paint_icon(dc, tip, rect.left, rect.bottom);
        rect.left += scaled(tip, 38);
    }
    const UINT align = tip->icon || tip->content.details[0] ? DT_LEFT : DT_CENTER;
    const UINT flags = align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (tip->content.details[0]) {
        RECT title = rect;
        title.top = scaled(tip, 6);
        title.bottom = scaled(tip, 29);
        DrawTextW(dc, tip->content.label, -1, &title, flags);
        rect.top = title.bottom;
        rect.bottom = scaled(tip, 46);
        SelectObject(dc, tip->details_font);
        SetTextColor(dc, RGB(181, 198, 215));
        DrawTextW(dc, tip->content.details, -1, &rect, flags);
        if (tip->content.progress >= 0) {
            RECT bar = {rect.left, scaled(tip, 50), rect.right, scaled(tip, 54)};
            HBRUSH track = CreateSolidBrush(RGB(62, 80, 96));
            FillRect(dc, &bar, track);
            DeleteObject(track);
            bar.right = bar.left + MulDiv(bar.right - bar.left, tip->content.progress, 100);
            HBRUSH fill = CreateSolidBrush(RGB(134, 212, 163));
            FillRect(dc, &bar, fill);
            DeleteObject(fill);
        }
    } else {
        DrawTextW(dc, tip->content.label, -1, &rect, flags);
    }
    SelectObject(dc, old_font);
    EndPaint(window, &paint);
}

static LRESULT CALLBACK tooltip_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
    }
    if (message == WM_PAINT) {
        const Tooltip *tip = (const Tooltip *)GetWindowLongPtrW(window, GWLP_USERDATA);
        if (tip && tip->font) {
            paint_tooltip(window, tip);
            return 0;
        }
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wparam, lparam);
}

static HWND foreground_game(void)
{
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    WCHAR class_name[32];
    if (!window) return NULL;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || IsIconic(window) ||
        !GetClassNameW(window, class_name, ARRAYSIZE(class_name)) ||
        lstrcmpiW(class_name, L"SDL_app") != 0) return NULL;
    return window;
}

static void hide_tooltip(HWND window)
{
    if (window && IsWindowVisible(window)) ShowWindow(window, SW_HIDE);
}

static int set_font(Tooltip *tip, UINT dpi)
{
    HFONT font = CreateFontW(-MulDiv(17, (int)dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE,
                             FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT details = CreateFontW(-MulDiv(13, (int)dpi, 96), 0, 0, 0, FW_NORMAL, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!font || !details) {
        if (font) DeleteObject(font);
        if (details) DeleteObject(details);
        return 0;
    }
    if (tip->font) DeleteObject(tip->font);
    if (tip->details_font) DeleteObject(tip->details_font);
    tip->font = font;
    tip->details_font = details;
    tip->dpi = dpi;
    return 1;
}

static int ensure_tooltip(Tooltip *tip, HWND game, POINT origin)
{
    if (tip->game == game && IsWindow(tip->window)) return 1;
    if (IsWindow(tip->window)) DestroyWindow(tip->window);
    tip->game = game;
    tip->window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  window_class, L"", WS_POPUP, origin.x, origin.y, 0, 0, game, NULL,
                                  worker.module, tip);
    if (!tip->window) return 0;
    if (!SetLayeredWindowAttributes(tip->window, 0, 238, LWA_ALPHA)) {
        DestroyWindow(tip->window);
        tip->window = NULL;
        return 0;
    }
    return 1;
}

static void update_tooltip(Tooltip *tip)
{
    Content content;
    AcquireSRWLockShared(&label_lock);
    content = pending;
    ReleaseSRWLockShared(&label_lock);

    HWND game = foreground_game();
    RECT client;
    POINT origin = {0};
    if (!content.label[0] || !game || !GetClientRect(game, &client) ||
        !ClientToScreen(game, &origin) || client.right < 160 || client.bottom < 100) {
        hide_tooltip(tip->window);
        return;
    }
    if (!ensure_tooltip(tip, game, origin)) return;

    UINT dpi = GetDpiForWindow(game);
    if (!dpi) dpi = 96;
    const int icon_changed = tip->content.item_id != content.item_id;
    const int metrics_changed = tip->dpi != dpi || icon_changed ||
        lstrcmpW(tip->content.label, content.label) != 0 ||
        lstrcmpW(tip->content.details, content.details) != 0;
    const int progress_changed = tip->content.progress != content.progress;
    if (metrics_changed) {
        if (tip->dpi != dpi && !set_font(tip, dpi)) return;
        HDC dc = GetDC(tip->window);
        if (!dc) return;
        HGDIOBJ old_font = SelectObject(dc, tip->font);
        SIZE size = {0};
        GetTextExtentPoint32W(dc, content.label, lstrlenW(content.label), &size);
        SelectObject(dc, tip->details_font);
        SIZE detail_size = {0};
        GetTextExtentPoint32W(dc, content.details, lstrlenW(content.details), &detail_size);
        SelectObject(dc, old_font);
        ReleaseDC(tip->window, dc);
        tip->text_width = max(size.cx, detail_size.cx);
        if (icon_changed) load_icon(tip, content.item_id);
    }
    tip->content = content;

    const int width = min(tip->text_width + scaled(tip, tip->icon ? 70 : 32), client.right * 4 / 5);
    const int height = scaled(tip, content.progress >= 0 ? 63 :
                              content.details[0] ? 53 : tip->icon ? 42 : 35);
    int x = origin.x + (client.right - width) / 2;
    if (content.position == OVERLAY_TOP_LEFT) x = origin.x + scaled(tip, 20);
    if (content.position == OVERLAY_TOP_RIGHT) x = origin.x + client.right - width - scaled(tip, 20);
    const int y = origin.y + scaled(tip, 20);
    const int corner = scaled(tip, 16);
    RECT current;
    if (!GetWindowRect(tip->window, &current)) return;
    const int resized = current.right - current.left != width || current.bottom - current.top != height;
    if (resized || current.left != x || current.top != y || !IsWindowVisible(tip->window)) {
        if (resized) {
            HRGN shape = CreateRoundRectRgn(0, 0, width + 1, height + 1, corner, corner);
            if (shape && !SetWindowRgn(tip->window, shape, FALSE)) DeleteObject(shape);
        }
        SetWindowPos(tip->window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (metrics_changed || progress_changed || resized) InvalidateRect(tip->window, NULL, FALSE);
}

static void CALLBACK on_foreground(HWINEVENTHOOK hook, DWORD event, HWND window,
                                  LONG object_id, LONG child_id, DWORD source_thread, DWORD time)
{
    (void)hook;
    (void)event;
    (void)window;
    (void)object_id;
    (void)child_id;
    (void)source_thread;
    (void)time;
    SetEvent(worker.changed);
}

static DWORD WINAPI run_overlay(void *unused)
{
    (void)unused;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = tooltip_proc;
    cls.hInstance = worker.module;
    cls.lpszClassName = window_class;
    if (RegisterClassW(&cls)) {
        Tooltip tip = {0};
        const HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(com))
            CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                             &IID_IWICImagingFactory, (void **)&tip.imaging);
        HANDLE events[] = {worker.stop, worker.changed};
        HWINEVENTHOOK focus = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                             NULL, on_foreground, 0, 0, WINEVENT_OUTOFCONTEXT);
        for (;;) {
            update_tooltip(&tip);
            DWORD result = MsgWaitForMultipleObjectsEx(ARRAYSIZE(events), events, PLACEMENT_POLL_MS,
                                                       QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
        if (focus) UnhookWinEvent(focus);
        if (IsWindow(tip.window)) DestroyWindow(tip.window);
        if (tip.font) DeleteObject(tip.font);
        if (tip.details_font) DeleteObject(tip.details_font);
        if (tip.icon) DeleteObject(tip.icon);
        if (tip.imaging) IWICImagingFactory_Release(tip.imaging);
        if (SUCCEEDED(com)) CoUninitialize();
        UnregisterClassW(window_class, worker.module);
    }
    // The loader has no shutdown callback. Keep this code mapped until the worker exits.
    FreeLibraryAndExitThread(worker.module, 0);
    return 0;
}

int overlay_start(void)
{
    AcquireSRWLockExclusive(&worker_lock);
    if (!worker.thread && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                             window_class, &worker.module)) {
        worker.stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        worker.changed = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (worker.stop && worker.changed)
            worker.thread = CreateThread(NULL, 0, run_overlay, NULL, 0, NULL);
        if (!worker.thread) {
            if (worker.stop) CloseHandle(worker.stop);
            if (worker.changed) CloseHandle(worker.changed);
            worker.stop = NULL;
            worker.changed = NULL;
            FreeLibrary(worker.module);
        }
    }
    const int running = worker.thread != NULL;
    ReleaseSRWLockExclusive(&worker_lock);
    return running;
}

void overlay_set(const char *label, const char *details, unsigned item_id, OverlayPosition position,
                 int progress)
{
    Content content = {.item_id = item_id, .position = position, .progress = progress};
    if (label && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label, -1,
                                     content.label, ARRAYSIZE(content.label)))
        content.label[0] = L'\0';
    if (details)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, details, -1,
                            content.details, ARRAYSIZE(content.details));

    AcquireSRWLockShared(&worker_lock);
    if (worker.thread) {
        AcquireSRWLockExclusive(&label_lock);
        pending = content;
        SetEvent(worker.changed);
        ReleaseSRWLockExclusive(&label_lock);
    }
    ReleaseSRWLockShared(&worker_lock);
}

void overlay_stop(void)
{
    AcquireSRWLockExclusive(&worker_lock);
    if (worker.thread) {
        SetEvent(worker.stop);
        WaitForSingleObject(worker.thread, INFINITE);
        CloseHandle(worker.thread);
        CloseHandle(worker.stop);
        CloseHandle(worker.changed);
        worker.thread = NULL;
        worker.stop = NULL;
        worker.changed = NULL;
        pending = (Content){0};
    }
    ReleaseSRWLockExclusive(&worker_lock);
}
