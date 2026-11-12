// Mini OSD: FPS / GPU / CPU overlay. One timer tick per second, no injection.
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <evntrace.h>
#include <evntcons.h>

enum { S_FPS, S_GPU, S_GPUT, S_CPU, S_CPUT, S_N };
static const wchar_t *KEYS[S_N] = { L"fps", L"gpu", L"gputemp", L"cpu", L"cputemp" };
static const wchar_t *NAMES[S_N] = { L"FPS", L"GPU usage", L"GPU temp", L"CPU usage", L"CPU temp" };
static BOOL on[S_N];
static wchar_t iniPath[MAX_PATH];
static wchar_t target[64]; // exe whose FPS we show; "" = entire screen (whatever window is in front)
static HWND overlay, trayWnd;
static UINT WM_TASKBARCREATED;
#define WM_TRAY (WM_APP + 1)
#define ID_HIDE 97
#define ID_PICK 98
#define ID_EXIT 99

// Diagnostics: start with /log to write osd.log next to the exe.
static HANDLE logFile;
static void logf(const wchar_t *f, ...)
{
    wchar_t w[512];
    char a[1024];
    DWORD n;
    va_list v;
    if (!logFile) return;
    va_start(v, f);
    wvsprintfW(w, f, v);
    va_end(v);
    WriteFile(logFile, a, WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a, NULL, NULL) - 1, &n, NULL);
}

static void exeOf(DWORD pid, wchar_t out[64])
{
    wchar_t path[MAX_PATH];
    DWORD n = MAX_PATH;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    out[0] = 0;
    if (!p) return;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        lstrcpynW(out, wcsrchr(path, L'\\') + 1, 64);
        CharLowerW(out);
    }
    CloseHandle(p);
}

// ---------- FPS: ETW present events from every graphics layer, so any window counts ----------
// Each source counts a process's presents on its own and we show the highest, so layers never double count:
//   DXGI Present_Start / PresentMultiplaneOverlay_Start (DX10-12, browsers, most apps), D3D9 Present_Start,
//   DxgKrnl Present_Info (OpenGL/Vulkan), DxgKrnl PresentHistory_Start + Flip_Info (composed / fullscreen).
// GUIDs, IDs and keywords from PresentMon's ETW headers.
static const GUID DXGI = { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
static const GUID D3D9 = { 0x783ACA0A, 0x790E, 0x4D7F, { 0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9 } };
static const GUID DXGKRNL = { 0x802EC45A, 0x1E99, 0x4B83, { 0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D } };
enum { SRC_DXGI, SRC_D3D9, SRC_KPRESENT, SRC_KHISTORY, SRC_N };
#define SESSION_NAME L"MiniOSD"
#define EVENT_FILTER_TYPE_EVENT_ID 0x80000200 // missing from mingw headers
typedef struct { BOOLEAN FilterIn; UCHAR Reserved; USHORT Count; USHORT Events[3]; } EVENT_ID_FILTER;
static struct { EVENT_TRACE_PROPERTIES p; wchar_t name[32]; } props;
static TRACEHANDLE session, consumer = INVALID_PROCESSTRACE_HANDLE;
static BOOL etwOk;

// Per-process present counters, written by the ETW thread.
typedef struct {
    DWORD pid;
    ULONGLONG seen;
    wchar_t exe[64];
    LONGLONG last[SRC_N], avg[SRC_N]; // last present time, smoothed frame time (100ns)
    int fps[SRC_N];
    ULONGLONG at[SRC_N];
} Slot;
static Slot slots[32];
static SRWLOCK lock = SRWLOCK_INIT;
static volatile LONG evCount; // diagnostics

static void WINAPI onPresent(PEVENT_RECORD e)
{
    USHORT id = e->EventHeader.EventDescriptor.Id;
    int src = IsEqualGUID(&e->EventHeader.ProviderId, &DXGI) ? SRC_DXGI
            : IsEqualGUID(&e->EventHeader.ProviderId, &D3D9) ? SRC_D3D9
            : id == 184 ? SRC_KPRESENT : SRC_KHISTORY;
    DWORD pid = e->EventHeader.ProcessId;
    LONGLONG t = e->EventHeader.TimeStamp.QuadPart; // 100ns units (no RAW_TIMESTAMP mode)
    ULONGLONG now = GetTickCount64();
    Slot *s = NULL, *old = slots;
    evCount++;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < 32 && !s; i++) {
        if (slots[i].pid == pid) s = &slots[i];
        else if (slots[i].seen < old->seen) old = &slots[i];
    }
    if (!s) { // new presenter: take the stalest slot
        s = old;
        ZeroMemory(s, sizeof *s);
        s->pid = pid;
        exeOf(pid, s->exe);
    }
    // Frame time averaged over ~16 frames: updates every frame, but stays steady. A gap over 1s restarts it.
    LONGLONG dt = t - s->last[src];
    if (s->last[src] && dt > 0 && dt < 10000000) {
        s->avg[src] = s->avg[src] ? s->avg[src] + (dt - s->avg[src]) / 16 : dt;
        s->fps[src] = (int)((10000000 + s->avg[src] / 2) / s->avg[src]);
        s->at[src] = now;
    } else if (dt >= 10000000) {
        s->avg[src] = 0;
    }
    s->last[src] = t;
    s->seen = now;
    ReleaseSRWLockExclusive(&lock);
}

// FPS of every process with that exe name (highest source, highest process wins).
static int fpsOf(const wchar_t *exe)
{
    int best = 0;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < 32; i++)
        if (slots[i].pid && !lstrcmpiW(slots[i].exe, exe))
            for (int k = 0; k < SRC_N; k++)
                if (now - slots[i].at[k] < 1000 && slots[i].fps[k] > best) best = slots[i].fps[k];
    ReleaseSRWLockShared(&lock);
    return best;
}

static DWORD WINAPI etwThread(LPVOID unused) { (void)unused; ProcessTrace(&consumer, 1, NULL, NULL); return 0; }

static void stopSession(void)
{
    props.p.Wnode.BufferSize = sizeof props;
    props.p.LoggerNameOffset = offsetof(typeof(props), name);
    ControlTraceW(0, SESSION_NAME, &props.p, EVENT_TRACE_CONTROL_STOP);
}

// Enable a provider, filtered at the source to just these event IDs.
static BOOL enable(const GUID *g, ULONGLONG any, ULONGLONG all, USHORT n, USHORT a, USHORT b, USHORT c)
{
    EVENT_ID_FILTER f = { TRUE, 0, n, { a, b, c } };
    EVENT_FILTER_DESCRIPTOR d = { (ULONGLONG)(ULONG_PTR)&f, offsetof(EVENT_ID_FILTER, Events) + n * sizeof(USHORT),
                                  EVENT_FILTER_TYPE_EVENT_ID }; // Size must agree with Count
    ENABLE_TRACE_PARAMETERS ep = { ENABLE_TRACE_PARAMETERS_VERSION_2, 0, 0, { 0 }, &d, 1 };
    ULONG rc = EnableTraceEx2(session, g, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION, any, all, 0, &ep);
    logf(L"EnableTraceEx2 %08x: %u\r\n", g->Data1, rc);
    return rc == ERROR_SUCCESS;
}

static void etwStart(void)
{
    stopSession(); // leftover from a crashed run
    ZeroMemory(&props, sizeof props);
    props.p.Wnode.BufferSize = sizeof props;
    props.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props.p.Wnode.ClientContext = 1; // QPC
    props.p.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | 0x10; // EVENT_TRACE_USE_MS_FLUSH_TIMER
    props.p.LoggerNameOffset = offsetof(typeof(props), name);
    props.p.BufferSize = 8; // KB; buffer counts left to ETW (it needs >= 2 per CPU)
    props.p.FlushTimer = 200; // ms: events reach us within one overlay tick
    ULONG rc = StartTraceW(&session, SESSION_NAME, &props.p);
    logf(L"StartTrace %u\r\n", rc);
    if (rc != ERROR_SUCCESS) return;

    // DxgKrnl: Base keyword only; never the Performance keyword (PresentMon: side effects on some Windows builds).
    BOOL ok = enable(&DXGI, 0x8000000000000002, 0x8000000000000002, 2, 42, 55, 0);
    ok |= enable(&D3D9, 0x8000000000000002, 0x8000000000000002, 1, 1, 0, 0);
    ok |= enable(&DXGKRNL, 0x8000001, 0x1, 3, 184, 171, 168);
    if (!ok) return;

    EVENT_TRACE_LOGFILEW lf = { 0 };
    lf.LoggerName = SESSION_NAME;
    lf.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    lf.EventRecordCallback = onPresent;
    consumer = OpenTraceW(&lf);
    if (consumer == INVALID_PROCESSTRACE_HANDLE) return;
    CloseHandle(CreateThread(NULL, 64 * 1024, etwThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL));
    etwOk = TRUE;
}

// ---------- GPU: NVML (ships with the NVIDIA driver) ----------
typedef struct { unsigned gpu, memory; } nvmlUtil;
static int (*nvmlInit)(void), (*nvmlShutdown)(void);
static int (*nvmlGetHandle)(unsigned, void **);
static int (*nvmlGetUtil)(void *, nvmlUtil *);
static int (*nvmlGetTemp)(void *, int, unsigned *);
static void *gpu;

static void nvmlStart(void)
{
    HMODULE m = LoadLibraryW(L"nvml.dll");
    if (!m) return;
    nvmlInit = (void *)GetProcAddress(m, "nvmlInit_v2");
    nvmlShutdown = (void *)GetProcAddress(m, "nvmlShutdown");
    nvmlGetHandle = (void *)GetProcAddress(m, "nvmlDeviceGetHandleByIndex_v2");
    nvmlGetUtil = (void *)GetProcAddress(m, "nvmlDeviceGetUtilizationRates");
    nvmlGetTemp = (void *)GetProcAddress(m, "nvmlDeviceGetTemperature");
    if (!nvmlInit || !nvmlShutdown || !nvmlGetHandle || !nvmlGetUtil || !nvmlGetTemp || nvmlInit() != 0) return;
    if (nvmlGetHandle(0, &gpu) != 0) gpu = NULL;
}

// ---------- CPU temp: PawnIO driver + IntelMSR module (same IOCTLs LibreHardwareMonitor uses) ----------
#define PIO_LOAD 0xA1B22084
#define PIO_EXEC 0xA1B22104
static HANDLE pawn = INVALID_HANDLE_VALUE;
static int tjMax;

static BOOL readMsr(ULONG64 idx, ULONG64 *v)
{
    struct { char fn[32]; ULONG64 idx; } in = { "ioctl_read_msr", idx };
    DWORD got = 0;
    return DeviceIoControl(pawn, PIO_EXEC, &in, sizeof in, v, sizeof *v, &got, NULL) && got == sizeof *v;
}

static void pawnStart(void)
{
    wchar_t path[MAX_PATH];
    pawn = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", FILE_READ_DATA | FILE_WRITE_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (pawn == INVALID_HANDLE_VALUE) return;

    // IntelMSR.bin sits next to the exe
    GetModuleFileNameW(NULL, path, MAX_PATH);
    lstrcpyW(wcsrchr(path, L'\\') + 1, L"IntelMSR.bin");
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BOOL ok = FALSE;
    if (f != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(f, NULL), got;
        UCHAR *blob = HeapAlloc(GetProcessHeap(), 0, size);
        if (blob && ReadFile(f, blob, size, &got, NULL) && got == size)
            ok = DeviceIoControl(pawn, PIO_LOAD, blob, size, NULL, 0, &got, NULL);
        HeapFree(GetProcessHeap(), 0, blob);
        CloseHandle(f);
    }
    ULONG64 v;
    if (ok && readMsr(0x1A2, &v)) tjMax = (v >> 16) & 0xFF; // MSR_TEMPERATURE_TARGET
    if (!tjMax) { CloseHandle(pawn); pawn = INVALID_HANDLE_VALUE; }
}

static int cpuTemp(void)
{
    ULONG64 v;
    if (pawn == INVALID_HANDLE_VALUE || !readMsr(0x1B1, &v)) return -1; // IA32_PACKAGE_THERM_STATUS
    return tjMax - (int)((v >> 16) & 0x7F);
}

// ---------- CPU usage ----------
static int cpuUsage(void)
{
    static ULONGLONG pIdle, pTotal;
    FILETIME i, k, u;
    GetSystemTimes(&i, &k, &u);
    ULONGLONG idle = ((ULONGLONG)i.dwHighDateTime << 32) | i.dwLowDateTime;
    ULONGLONG total = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) +
                      (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime); // kernel includes idle
    ULONGLONG di = idle - pIdle, dt = total - pTotal;
    pIdle = idle; pTotal = total;
    return dt ? (int)(100 - (di * 100 + dt / 2) / dt) : 0;
}

// ---------- picked window ----------
static HWND targetWnd; // window the overlay follows; NULL with target "" (entire screen)

// Top-level app windows the picker offers; fills exe.
static BOOL pickable(HWND h, wchar_t exe[64])
{
    DWORD pid, cloaked = 0;
    wchar_t cls[16];
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        !GetWindowTextLengthW(h)) return FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    GetClassNameW(h, cls, 16);
    GetWindowThreadProcessId(h, &pid);
    if (cloaked || !lstrcmpW(cls, L"Progman") || pid == GetCurrentProcessId()) return FALSE;
    exeOf(pid, exe);
    return TRUE;
}

static BOOL CALLBACK matchTarget(HWND h, LPARAM out)
{
    wchar_t exe[64];
    if (!pickable(h, exe) || lstrcmpiW(exe, target)) return TRUE;
    *(HWND *)out = h;
    return FALSE;
}

// The picked window, re-found by exe name after the app restarts.
static HWND targetWindow(void)
{
    if (!target[0]) return NULL;
    if (!IsWindow(targetWnd)) {
        targetWnd = NULL;
        EnumWindows(matchTarget, (LPARAM)&targetWnd);
    }
    return targetWnd;
}

// ---------- overlay: NVIDIA-style one-line HUD, per-pixel alpha ----------
// "FPS 13 | GPU 95% 64°C | CPU 51% 58°C": white text, bold values, small units, thin dividers, soft dark outline.
typedef struct { HFONT f; int gap; wchar_t s[12]; } Run; // f == NULL: divider
static Run runs[16];
static int nRuns, em;
static HFONT fLabel, fValue, fUnit;

static void addRun(HFONT f, int gap, const wchar_t *s)
{
    Run *r = &runs[nRuns++];
    r->f = f; r->gap = gap;
    lstrcpynW(r->s, s, 12);
}

static void addLabel(const wchar_t *s)
{
    if (nRuns) addRun(NULL, em * 3 / 5, L"");
    addRun(fLabel, nRuns ? em * 3 / 5 : 0, s);
}

static void addValue(int v, const wchar_t *unit, int gap)
{
    wchar_t b[12];
    if (v < 0) { addRun(fValue, gap, L"--"); return; }
    wsprintfW(b, L"%d", v);
    addRun(fValue, gap, b);
    addRun(fUnit, em / 10, unit);
}

// Overlay sits at the top right of `anchor` (the picked window's client area, or the screen).
static void render(const RECT *anchor)
{
    static HDC dc;
    static HBITMAP bmp;
    static DWORD *px;
    static BYTE *mask;
    static int bw, bh; // DIB size; only grows
    static Run prev[16];
    static int prevN;
    static RECT prevAnchor;
    if (!nRuns) { ShowWindow(overlay, SW_HIDE); return; }
    // Same text in the same place: nothing to redraw.
    if (nRuns == prevN && EqualRect(anchor, &prevAnchor) && !memcmp(runs, prev, nRuns * sizeof(Run)) &&
        IsWindowVisible(overlay)) return;
    prevN = nRuns;
    prevAnchor = *anchor;
    CopyMemory(prev, runs, nRuns * sizeof(Run));
    if (!dc) dc = CreateCompatibleDC(NULL);

    TEXTMETRICW tm;
    SIZE sz;
    SelectObject(dc, fValue);
    GetTextMetricsW(dc, &tm);
    int pad = 3, divW = em > 24 ? 2 : 1;
    int w = 2 * pad, h = tm.tmHeight + 2 * pad, base = pad + tm.tmAscent;
    for (int i = 0; i < nRuns; i++) {
        w += runs[i].gap;
        if (!runs[i].f) { w += divW; continue; }
        SelectObject(dc, runs[i].f);
        GetTextExtentPoint32W(dc, runs[i].s, lstrlenW(runs[i].s), &sz);
        w += sz.cx;
    }
    if (w > bw || h > bh) {
        if (w > bw) bw = w;
        if (h > bh) bh = h;
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), bw, -bh, 1, 32 } };
        HBITMAP nb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
        SelectObject(dc, nb);
        if (bmp) DeleteObject(bmp);
        bmp = nb;
        if (mask) HeapFree(GetProcessHeap(), 0, mask);
        mask = HeapAlloc(GetProcessHeap(), 0, bw * bh);
    }

    // Draw white-on-black: the green channel becomes text coverage.
    ZeroMemory(px, bw * bh * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    int x = pad;
    for (int i = 0; i < nRuns; i++) {
        x += runs[i].gap;
        if (!runs[i].f) {
            RECT d = { x, base - (tm.tmAscent - tm.tmInternalLeading) - 1, x + divW, base + tm.tmDescent / 2 };
            FillRect(dc, &d, GetStockObject(WHITE_BRUSH));
            x += divW;
            continue;
        }
        SelectObject(dc, runs[i].f);
        int len = lstrlenW(runs[i].s);
        TextOutW(dc, x, base, runs[i].s, len);
        GetTextExtentPoint32W(dc, runs[i].s, len, &sz);
        x += sz.cx;
    }
    GdiFlush();

    // Coverage -> premultiplied white text with a 1px dark outline (max of 3x3 neighbourhood).
    for (int i = 0; i < bw * bh; i++) mask[i] = (BYTE)(px[i] >> 8);
    for (int y = 0; y < h; y++)
        for (int xx = 0; xx < w; xx++) {
            int a = mask[y * bw + xx], o = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int yy = y + dy, xc = xx + dx;
                    if (yy >= 0 && yy < h && xc >= 0 && xc < w && mask[yy * bw + xc] > o) o = mask[yy * bw + xc];
                }
            int A = a + (o * 150 / 255) * (255 - a) / 255;
            px[y * bw + xx] = (DWORD)A << 24 | a << 16 | a << 8 | a;
        }

    POINT dst = { anchor->right - w - em / 2, anchor->top + em / 2 }, src = { 0, 0 };
    SIZE size = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(overlay, NULL, &dst, &size, dc, &src, 0, &bf, ULW_ALPHA);
    if (!IsWindowVisible(overlay)) ShowWindow(overlay, SW_SHOWNOACTIVATE);
}

static int slowTick; // 0 = refresh GPU/CPU on the next tick

static BOOL hidden; // "Hide overlay" in the tray: no sampling, no drawing

static void sample(void)
{
    if (hidden) { ShowWindow(overlay, SW_HIDE); return; }
    RECT anchor = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    HWND tw = targetWindow();
    nRuns = 0;
    ZeroMemory(runs, sizeof runs); // so render() can compare runs byte for byte
    if (target[0] && (!tw || IsIconic(tw))) { // picked app closed or minimized: nothing to sit on
        ShowWindow(overlay, SW_HIDE);
        return;
    }
    if (tw) {
        GetClientRect(tw, &anchor);
        MapWindowPoints(tw, NULL, (POINT *)&anchor, 2);
    }
    if (on[S_FPS]) {
        int v = -1;
        if (etwOk && target[0]) v = fpsOf(target);
        else if (etwOk) { // entire screen: whatever app is in front
            static DWORD fgPid;
            static wchar_t fgExe[64];
            DWORD pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            if (pid != fgPid) { fgPid = pid; exeOf(pid, fgExe); }
            v = fgExe[0] ? fpsOf(fgExe) : 0;
        }
        addLabel(L"FPS");
        addValue(v, L"", em / 2);
    }
    // GPU/CPU refresh once a second (every 3rd tick); FPS refreshes every tick.
    static int gpuU = -1, gpuT = -1, cpuU = -1, cpuT = -1;
    if (slowTick++ % 3 == 0) {
        nvmlUtil u;
        unsigned t;
        if (on[S_GPU]) gpuU = gpu && nvmlGetUtil(gpu, &u) == 0 ? (int)u.gpu : -1;
        if (on[S_GPUT]) gpuT = gpu && nvmlGetTemp(gpu, 0, &t) == 0 ? (int)t : -1;
        if (on[S_CPU]) cpuU = cpuUsage();
        if (on[S_CPUT]) cpuT = cpuTemp();
    }
    if (on[S_GPU] || on[S_GPUT]) {
        addLabel(L"GPU");
        if (on[S_GPU]) addValue(gpuU, L"%", em / 3);
        if (on[S_GPUT]) addValue(gpuT, L"\x00B0" L"C", on[S_GPU] ? em / 2 : em / 3);
    }
    if (on[S_CPU] || on[S_CPUT]) {
        addLabel(L"CPU");
        if (on[S_CPU]) addValue(cpuU, L"%", em / 3);
        if (on[S_CPUT]) addValue(cpuT, L"\x00B0" L"C", on[S_CPU] ? em / 2 : em / 3);
    }
    render(&anchor);
    if (logFile) {
        ULONGLONG now = GetTickCount64();
        logf(L"events=%d target=%s\r\n", evCount, target);
        AcquireSRWLockShared(&lock);
        for (int i = 0; i < 32; i++)
            if (slots[i].pid && now - slots[i].seen < 2000) // fps per source: dxgi d3d9 kpresent khistory
                logf(L"  pid=%u %s fps=%d/%d/%d/%d\r\n", slots[i].pid, slots[i].exe,
                     slots[i].fps[0], slots[i].fps[1], slots[i].fps[2], slots[i].fps[3]);
        ReleaseSRWLockShared(&lock);
    }
    // Games sometimes push themselves above other topmost windows.
    SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ---------- capture picker (Discord-style): live DWM thumbnails, cost nothing while closed ----------
#define PICK_MAX 16 // ponytail: 1 screen + 15 windows, no scrolling; add a scrollbar if you run more apps
#define COLS 4
#define TW 240
#define TH 135
#define TXT 46
#define GAP 14
typedef struct { HWND h; HTHUMBNAIL th; wchar_t title[80], exe[64]; } Tile;
static Tile tiles[PICK_MAX];
static int nTiles, hover = -1;
static HWND pickWnd;
static HBITMAP screenThumb;
static HFONT pickFont, pickSmall;

static RECT tileRect(int i)
{
    RECT r = { GAP + (i % COLS) * (TW + GAP), GAP + (i / COLS) * (TH + TXT + GAP), 0, 0 };
    r.right = r.left + TW;
    r.bottom = r.top + TH + TXT;
    return r;
}

static RECT thumbRect(int i)
{
    RECT r = tileRect(i);
    InflateRect(&r, -6, -6);
    r.bottom = r.top + TH - 6;
    return r;
}

static void fill(HDC dc, const RECT *r, COLORREF c) { SetDCBrushColor(dc, c); FillRect(dc, r, GetStockObject(DC_BRUSH)); }

static BOOL CALLBACK addWindow(HWND h, LPARAM unused)
{
    (void)unused;
    Tile *t = &tiles[nTiles];
    // ponytail: UWP apps report ApplicationFrameHost.exe; pick "Entire screen" for those
    if (nTiles >= PICK_MAX || !pickable(h, t->exe)) return TRUE;
    GetWindowTextW(h, t->title, 80);
    t->h = h;
    t->th = NULL;
    nTiles++;
    return TRUE;
}

static void openPicker(void)
{
    if (pickWnd) { SetForegroundWindow(pickWnd); return; }
    if (!pickFont) {
        pickFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        pickSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }
    nTiles = 1;
    tiles[0] = (Tile){ NULL, NULL, L"Entire screen", L"" };
    EnumWindows(addWindow, 0);

    // Still snapshot of the screen for the "Entire screen" tile.
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    RECT tr = thumbRect(0);
    HDC s = GetDC(NULL), m = CreateCompatibleDC(s);
    screenThumb = CreateCompatibleBitmap(s, tr.right - tr.left, tr.bottom - tr.top);
    SelectObject(m, screenThumb);
    SetStretchBltMode(m, HALFTONE);
    StretchBlt(m, 0, 0, tr.right - tr.left, tr.bottom - tr.top, s, 0, 0, sw, sh, SRCCOPY);
    DeleteDC(m);
    ReleaseDC(NULL, s);

    int cols = nTiles < COLS ? nTiles : COLS, rows = (nTiles + COLS - 1) / COLS;
    RECT r = { 0, 0, GAP + cols * (TW + GAP), GAP + rows * (TH + TXT + GAP) };
    DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    pickWnd = CreateWindowExW(0, L"MiniOSDPick", L"Choose what to capture", style,
                              (sw - w) / 2, (sh - h) / 2, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(pickWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);

    for (int i = 1; i < nTiles; i++) {
        SIZE src;
        if (DwmRegisterThumbnail(pickWnd, tiles[i].h, &tiles[i].th) != S_OK) { tiles[i].th = NULL; continue; }
        if (DwmQueryThumbnailSourceSize(tiles[i].th, &src) != S_OK || !src.cx || !src.cy) continue;
        RECT d = thumbRect(i); // fit, keeping aspect ratio
        int bw = d.right - d.left, bh = d.bottom - d.top;
        if (src.cx * bh > src.cy * bw) { int fh = src.cy * bw / src.cx; d.top += (bh - fh) / 2; d.bottom = d.top + fh; }
        else { int fw = src.cx * bh / src.cy; d.left += (bw - fw) / 2; d.right = d.left + fw; }
        DWM_THUMBNAIL_PROPERTIES p = { DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY };
        p.rcDestination = d;
        p.fVisible = TRUE;
        p.fSourceClientAreaOnly = TRUE;
        DwmUpdateThumbnailProperties(tiles[i].th, &p);
    }
    ShowWindow(pickWnd, SW_SHOW);
    SetForegroundWindow(pickWnd);
    SetTimer(pickWnd, 1, 1000, NULL); // refresh the live FPS under each tile
}

static void pickPaint(HWND hw)
{
    PAINTSTRUCT ps;
    RECT c;
    HDC dc = BeginPaint(hw, &ps);
    GetClientRect(hw, &c);
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateCompatibleBitmap(dc, c.right, c.bottom);
    HGDIOBJ ob = SelectObject(m, b);
    fill(m, &c, RGB(30, 31, 34));
    SetBkMode(m, TRANSPARENT);
    for (int i = 0; i < nTiles; i++) {
        RECT r = tileRect(i), in = r, tr = thumbRect(i);
        BOOL sel = i == 0 ? !target[0] : target[0] && !lstrcmpiW(tiles[i].exe, target);
        fill(m, &r, sel ? RGB(118, 185, 0) : i == hover ? RGB(100, 102, 110) : RGB(43, 45, 49));
        InflateRect(&in, -2, -2);
        fill(m, &in, RGB(43, 45, 49));
        fill(m, &tr, RGB(17, 18, 20));
        if (i == 0) {
            HDC t = CreateCompatibleDC(m);
            HGDIOBJ o = SelectObject(t, screenThumb);
            BitBlt(m, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, t, 0, 0, SRCCOPY);
            SelectObject(t, o);
            DeleteDC(t);
        }
        wchar_t sub[96];
        int f = i ? fpsOf(tiles[i].exe) : 0;
        if (i == 0) lstrcpyW(sub, L"Follows the window in front");
        else if (f > 0) wsprintfW(sub, L"%s  \x2022  %d FPS", tiles[i].exe, f);
        else lstrcpyW(sub, tiles[i].exe);
        RECT t1 = { r.left + 10, r.top + TH + 4, r.right - 10, r.top + TH + 24 };
        RECT t2 = { r.left + 10, r.top + TH + 24, r.right - 10, r.top + TH + 42 };
        SelectObject(m, pickFont);
        SetTextColor(m, RGB(242, 243, 245));
        DrawTextW(m, tiles[i].title, -1, &t1, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(m, pickSmall);
        SetTextColor(m, f > 0 ? RGB(118, 185, 0) : RGB(160, 163, 170));
        DrawTextW(m, sub, -1, &t2, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    BitBlt(dc, 0, 0, c.right, c.bottom, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(b);
    DeleteDC(m);
    EndPaint(hw, &ps);
}

static int hitTile(LPARAM l)
{
    POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
    for (int i = 0; i < nTiles; i++) {
        RECT r = tileRect(i);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

static LRESULT CALLBACK pickProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    int i;
    switch (msg) {
    case WM_PAINT: pickPaint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_MOUSEMOVE:
        if ((i = hitTile(l)) != hover) { hover = i; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if ((i = hitTile(l)) >= 0) {
            lstrcpyW(target, tiles[i].exe);
            targetWnd = tiles[i].h;
            WritePrivateProfileStringW(L"osd", L"target", target, iniPath);
            DestroyWindow(h);
            sample();
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        for (i = 1; i < nTiles; i++) if (tiles[i].th) DwmUnregisterThumbnail(tiles[i].th);
        DeleteObject(screenThumb);
        screenThumb = NULL;
        pickWnd = NULL;
        hover = -1;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ---------- tray ----------
static void trayIcon(DWORD msg)
{
    NOTIFYICONDATAW n = { sizeof n };
    n.hWnd = trayWnd;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wsprintfW(n.szTip, L"Mini OSD - FPS: %s  GPU: %s  CPU temp: %s",
              etwOk ? L"ok" : L"off", gpu ? L"ok" : L"off", pawn != INVALID_HANDLE_VALUE ? L"ok" : L"off");
    Shell_NotifyIconW(msg, &n);
}

static void trayMenu(void)
{
    POINT pt;
    wchar_t cap[96];
    HMENU m = CreatePopupMenu();
    wsprintfW(cap, L"Capture: %s...", target[0] ? target : L"Entire screen");
    AppendMenuW(m, MF_STRING | (hidden ? MF_CHECKED : 0), ID_HIDE, L"Hide overlay");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_PICK, cap);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < S_N; i++)
        AppendMenuW(m, MF_STRING | (on[i] ? MF_CHECKED : 0), i + 1, NAMES[i]);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
    GetCursorPos(&pt);
    SetForegroundWindow(trayWnd); // required so the menu closes when clicking elsewhere
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, trayWnd, NULL);
    DestroyMenu(m);
    if (cmd == ID_EXIT) { DestroyWindow(trayWnd); return; }
    if (cmd == ID_PICK) { openPicker(); return; }
    if (cmd == ID_HIDE) { hidden = !hidden; slowTick = 0; sample(); return; }
    if (cmd >= 1 && cmd <= S_N) {
        int i = cmd - 1;
        on[i] = !on[i];
        WritePrivateProfileStringW(L"osd", KEYS[i], on[i] ? L"1" : L"0", iniPath);
        slowTick = 0; // a stat just switched on shows its value right away
        sample();
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_TIMER: sample(); return 0;
    case WM_TRAY:
        if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_LBUTTONUP) trayMenu();
        return 0;
    case WM_DESTROY:
        if (h == trayWnd) PostQuitMessage(0);
        return 0;
    }
    if (WM_TASKBARCREATED && msg == WM_TASKBARCREATED) { trayIcon(NIM_ADD); return 0; } // explorer restarted
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)show;
    // Launching again replaces the running copy.
    HANDLE single = CreateMutexW(NULL, TRUE, L"MiniOSD.single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"MiniOSD", L"MiniOSD");
        if (old) PostMessageW(old, WM_CLOSE, 0, 0);
        WaitForSingleObject(single, 5000); // a stuck old copy must not block the new one
    }

    // EcoQoS: run on efficiency scheduling, we never need speed.
    PROCESS_POWER_THROTTLING_STATE pt = { PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                          PROCESS_POWER_THROTTLING_EXECUTION_SPEED, PROCESS_POWER_THROTTLING_EXECUTION_SPEED };
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);

    GetModuleFileNameW(NULL, iniPath, MAX_PATH);
    lstrcpyW(wcsrchr(iniPath, L'.'), L".ini");
    for (int i = 0; i < S_N; i++) on[i] = GetPrivateProfileIntW(L"osd", KEYS[i], 1, iniPath);
    GetPrivateProfileStringW(L"osd", L"target", L"", target, 64, iniPath);
    if (wcsstr(cmd, L"/log")) {
        wchar_t lp[MAX_PATH];
        lstrcpyW(lp, iniPath);
        lstrcpyW(wcsrchr(lp, L'.'), L".log");
        logFile = CreateFileW(lp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        if (logFile == INVALID_HANDLE_VALUE) logFile = NULL;
    }

    etwStart();
    nvmlStart();
    pawnStart();
    cpuUsage(); // prime the delta

    // NVIDIA's overlay uses a DIN-style face; Bahnschrift (built into Windows 10) is the closest match.
    em = GetSystemMetrics(SM_CYSCREEN) * 16 / 1080;
    fLabel = CreateFontW(-em, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Bahnschrift");
    fValue = CreateFontW(-em, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Bahnschrift");
    fUnit = CreateFontW(-em * 3 / 4, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Bahnschrift");

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"MiniOSD";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = pickProc;
    wc.lpszClassName = L"MiniOSDPick";
    RegisterClassW(&wc);

    overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              L"MiniOSD", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"MiniOSD", L"MiniOSD", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);

    trayIcon(NIM_ADD);
    sample();
    SetTimer(overlay, 1, 333, NULL); // FPS 3x a second; GPU/CPU every 3rd tick = 1s
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    trayIcon(NIM_DELETE);
    if (session) stopSession(); // ProcessTrace returns once the session stops
    if (consumer != INVALID_PROCESSTRACE_HANDLE) CloseTrace(consumer);
    if (gpu) nvmlShutdown();
    // Skip DLL teardown: nvml.dll's unload can hang the exit, leaving a zombie that holds the single-instance lock.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
