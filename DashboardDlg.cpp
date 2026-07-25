// DashboardDlg.cpp - 图表 + 空调控制面板实现（纯Win32 + GDI+，无RC文件）
#include "pch.h"
#include "DashboardDlg.h"
#include "MijiaPowerPlugin.h"
#include "MiioDevice.h"
#include <commctrl.h>
#include <ole2.h>          // GDI+ 依赖 COM 类型(interface/PROPID 等)，WIN32_LEAN_AND_MEAN 会排除，需显式引入
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

// ─── 控件 ID ───
#define IDC_STATUS       2001
#define IDC_BTN_10M      2010
#define IDC_BTN_1H       2011
#define IDC_BTN_24H      2012
#define IDC_BTN_7D       2013

#define IDC_BTN_POWER    2020
#define IDC_BTN_TDN      2021
#define IDC_STATIC_TEMP  2022
#define IDC_BTN_TUP      2023
#define IDC_COMBO_MODE   2024
#define IDC_COMBO_FAN    2025
#define IDC_BTN_SWING    2026
#define IDC_BTN_REFRESH  2027
#define IDC_BTN_DETECT   2028
#define IDC_BTN_TEST     2029
#define IDC_STATIC_AC    2030
#define IDC_STATIC_ERR   2031

static const wchar_t* MODE_LABELS[] = { L"自动", L"制冷", L"除湿", L"送风", L"制热" };
static const wchar_t* FAN_LABELS[]  = { L"自动", L"低", L"中", L"高" };

// ─── 单实例句柄 / 全局字体 / 主题 ───
static HWND   g_hDashboard = nullptr;
static HFONT  g_hFont = nullptr;      // Segoe UI 常规
static HFONT  g_hFontBig = nullptr;   // 温度大字
static HFONT  g_hFontTitle = nullptr; // 标题

struct Theme {
    bool dark;
    COLORREF bg, text, sub, grid, cardBorder;
    COLORREF btnFace, btnText, btnActive, btnHover;
    COLORREF powerLine, energyLine;   // 蓝青 / 橙
};
static Theme g_theme;
static HBRUSH g_hbrBg = nullptr;   // 静态控件背景刷（= 窗口背景色）

static bool IsSystemDark() {
    HKEY k; DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(k);
    }
    return val == 0;   // 0 = 深色
}
static void InitTheme() {
    g_theme.dark = IsSystemDark();
    if (g_theme.dark) {
        g_theme.bg = RGB(30,30,36);     g_theme.text = RGB(232,234,240); g_theme.sub = RGB(150,154,166);
        g_theme.grid = RGB(54,54,62);   g_theme.cardBorder = RGB(60,60,70);
        g_theme.btnFace = RGB(50,50,58);g_theme.btnText = RGB(228,230,236);
        g_theme.btnActive = RGB(0,172,172); g_theme.btnHover = RGB(64,64,74);
        g_theme.powerLine = RGB(38,194,188); g_theme.energyLine = RGB(240,150,60);
    } else {
        g_theme.bg = RGB(244,245,247);  g_theme.text = RGB(28,30,36);   g_theme.sub = RGB(108,114,128);
        g_theme.grid = RGB(228,232,238);g_theme.cardBorder = RGB(216,222,230);
        g_theme.btnFace = RGB(255,255,255); g_theme.btnText = RGB(40,42,50);
        g_theme.btnActive = RGB(0,162,162);  g_theme.btnHover = RGB(238,240,244);
        g_theme.powerLine = RGB(13,148,136); g_theme.energyLine = RGB(234,120,40);
    }
    if (g_hbrBg) DeleteObject(g_hbrBg);
    g_hbrBg = CreateSolidBrush(g_theme.bg);
}
static Color C(COLORREF c, int a = 255) {
    return Color((BYTE)a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ─── DPI 辅助 ───
static UINT GetWindowDpi(HWND hWnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 && hWnd) {
        typedef UINT(WINAPI* PFN)(HWND);
        auto pfn = (PFN)GetProcAddress(user32, "GetDpiForWindow");
        if (pfn) { UINT d = pfn(hWnd); if (d) return d; }
    }
    HDC dc = GetDC(hWnd);
    UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hWnd, dc);
    return d ? d : 96;
}

static HWND AddCtrl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style,
                    int x, int y, int w, int h, int id, HFONT hFont) {
    HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(intptr_t)id,
        (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
    if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hw;
}

// 圆角矩形路径
static GraphicsPath* RoundedRect(int x, int y, int w, int h, int r) {
    GraphicsPath* p = new GraphicsPath();
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    p->AddArc(x, y, r, r, 180, 90);
    p->AddArc(x + w - r, y, r, r, 270, 90);
    p->AddArc(x + w - r, y + h - r, r, r, 0, 90);
    p->AddArc(x, y + h - r, r, r, 90, 90);
    p->CloseFigure();
    return p;
}

// ─── 显示 / 激活 ───
void CDashboardDlg::Show(HWND hParent, CMijiaPowerPlugin* plugin) {
    if (!plugin) return;
    if (g_hDashboard && IsWindow(g_hDashboard)) { SetForegroundWindow(g_hDashboard); return; }
    InitTheme();
    HINSTANCE hInst = hParent ? (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE)
                              : GetModuleHandleW(NULL);

    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = nullptr;                 // 自绘背景
        wc.lpszClassName = L"MijiaDashboardWnd";
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        atom = RegisterClassExW(&wc);
    }

    UINT dpi = GetWindowDpi(hParent ? hParent : nullptr);
    int W = MulDiv(840, dpi, 96);
    int H = MulDiv(720, dpi, 96);

    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (hParent) {
        RECT rc{}; GetWindowRect(hParent, &rc);
        px = rc.left + (rc.right - rc.left - W) / 2;
        py = rc.top + (rc.bottom - rc.top - H) / 2;
        if (px < 0) px = 0; if (py < 0) py = 0;
    }

    Ctx* ctx = new Ctx();
    ctx->plugin = plugin;

    HWND hWnd = CreateWindowExW(0, L"MijiaDashboardWnd",
        L"米家空调伴侣 · 图表与控制",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        px, py, W, H, hParent, NULL, hInst, ctx);
    if (!hWnd) { delete ctx; return; }
    g_hDashboard = hWnd;
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
}

// ─── 初始化控件 ───
void CDashboardDlg::OnInitDialog(HWND hWnd, Ctx* ctx) {
    UINT dpi = GetWindowDpi(hWnd);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    auto mkFont = [&](int px, int weight, bool big = false) {
        LOGFONTW lf = {};
        lf.lfHeight = -MulDiv(px, dpi, 96);
        lf.lfWeight = weight;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        HFONT f = CreateFontIndirectW(&lf);
        return f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    };
    g_hFont      = mkFont(14, FW_NORMAL);
    g_hFontBig   = mkFont(26, FW_BOLD);
    g_hFontTitle = mkFont(18, FW_SEMIBOLD);
    SetPropW(hWnd, L"MFONT", g_hFont);

    DWORD BTN = BS_PUSHBUTTON | BS_OWNERDRAW;   // 自绘扁平按钮

    // 顶部状态由画布绘制（头部标题 + 状态行），不创建控件避免与画布重叠
    ctx->hStatus = nullptr;

    // 时间范围按钮
    AddCtrl(hWnd, L"BUTTON", L"10分钟", BTN, S(20),  S(60), S(92), S(28), IDC_BTN_10M, g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"●1小时", BTN, S(120), S(60), S(92), S(28), IDC_BTN_1H,  g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"24小时", BTN, S(220), S(60), S(92), S(28), IDC_BTN_24H, g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"7天",    BTN, S(320), S(60), S(92), S(28), IDC_BTN_7D,  g_hFont);

    ctx->acEnabled = ConfigManager::Instance().Get().enableAcControl;

    // 空调控制行1：电源 / 温度± / 模式 / 风速 / 摆风
    int y1 = S(520);
    ctx->hPower = AddCtrl(hWnd, L"BUTTON", L"开机", BTN, S(28), y1, S(118), S(34), IDC_BTN_POWER, g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"−", BTN, S(170), y1, S(32), S(34), IDC_BTN_TDN, g_hFont);
    ctx->hTemp  = AddCtrl(hWnd, L"STATIC", L"--℃", SS_CENTER | SS_CENTERIMAGE, S(208), y1, S(76), S(34), IDC_STATIC_TEMP, g_hFontBig);
    AddCtrl(hWnd, L"BUTTON", L"+", BTN, S(290), y1, S(32), S(34), IDC_BTN_TUP, g_hFont);

    AddCtrl(hWnd, L"STATIC", L"模式", 0, S(344), y1, S(38), S(34), 0, g_hFont);
    ctx->hMode  = AddCtrl(hWnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, S(386), y1, S(118), S(200), IDC_COMBO_MODE, g_hFont);
    for (int i = 0; i < 5; i++) SendMessageW(ctx->hMode, CB_ADDSTRING, 0, (LPARAM)MODE_LABELS[i]);

    AddCtrl(hWnd, L"STATIC", L"风速", 0, S(518), y1, S(38), S(34), 0, g_hFont);
    ctx->hFan   = AddCtrl(hWnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, S(560), y1, S(110), S(200), IDC_COMBO_FAN, g_hFont);
    for (int i = 0; i < 4; i++) SendMessageW(ctx->hFan, CB_ADDSTRING, 0, (LPARAM)FAN_LABELS[i]);

    ctx->hSwing = AddCtrl(hWnd, L"BUTTON", L"摆风: 关", BTN, S(686), y1, S(118), S(34), IDC_BTN_SWING, g_hFont);

    // 行2：刷新 / 探测 / 测试 / 状态
    int y2 = S(612);
    AddCtrl(hWnd, L"BUTTON", L"刷新",     BTN, S(28),  y2, S(86),  S(28), IDC_BTN_REFRESH, g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"探测型号", BTN, S(122), y2, S(104), S(28), IDC_BTN_DETECT,  g_hFont);
    AddCtrl(hWnd, L"BUTTON", L"测试指令", BTN, S(236), y2, S(86),  S(28), IDC_BTN_TEST,    g_hFont);
    ctx->hAcStatus = AddCtrl(hWnd, L"STATIC", L"", SS_LEFT, S(332), y2, S(472), S(28), IDC_STATIC_AC, g_hFont);
    ctx->hErr      = AddCtrl(hWnd, L"STATIC", L"", SS_LEFT, S(28),  S(654), S(780), S(24), IDC_STATIC_ERR, g_hFont);

    if (!ctx->acEnabled) {
        for (HWND h : { ctx->hPower, ctx->hTemp, ctx->hMode, ctx->hFan, ctx->hSwing,
                        GetDlgItem(hWnd, IDC_BTN_TDN), GetDlgItem(hWnd, IDC_BTN_TUP),
                        GetDlgItem(hWnd, IDC_BTN_REFRESH), GetDlgItem(hWnd, IDC_BTN_TEST) }) {
            if (h) EnableWindow(h, FALSE);
        }
        SetWindowTextW(ctx->hErr, L"提示：在插件“选项”中勾选“启用空调控制”后即可使用控制面板。");
    }

    SetTimer(hWnd, 1, 2000, NULL);
}

// ─── 读取空调状态并更新 UI ───
void CDashboardDlg::RefreshAc(Ctx* ctx) {
    if (!ctx->acEnabled) return;
    ctx->plugin->AcRefreshState(ctx->ac);
    int pw = 0;
    if (ctx->plugin->AcGetPowerState(pw)) ctx->ac.power = pw;
    UpdateAcUi(ctx);
}

void CDashboardDlg::UpdateAcUi(Ctx* ctx) {
    if (ctx->ac.power == 1) SetWindowTextW(ctx->hPower, L"关机");
    else if (ctx->ac.power == 0) SetWindowTextW(ctx->hPower, L"开机");
    else SetWindowTextW(ctx->hPower, L"电源?");

    if (ctx->ac.temp >= 0) {
        wchar_t buf[32]; swprintf_s(buf, L"%d℃", ctx->ac.temp);
        SetWindowTextW(ctx->hTemp, buf);
    } else SetWindowTextW(ctx->hTemp, L"--℃");

    if (ctx->ac.mode >= 0 && ctx->ac.mode <= 4)
        SendMessageW(ctx->hMode, CB_SETCURSEL, ctx->ac.mode, 0);
    if (ctx->ac.fan >= 0 && ctx->ac.fan <= 3)
        SendMessageW(ctx->hFan, CB_SETCURSEL, ctx->ac.fan, 0);

    if (ctx->ac.swing == 1) SetWindowTextW(ctx->hSwing, L"摆风: 开");
    else if (ctx->ac.swing == 0) SetWindowTextW(ctx->hSwing, L"摆风: 关");
    else SetWindowTextW(ctx->hSwing, L"摆风: ?");

    std::wstring s = L"电源:" + std::wstring(ctx->ac.power == 1 ? L"开" : (ctx->ac.power == 0 ? L"关" : L"?"))
        + L"  模式:" + (ctx->ac.mode >= 0 && ctx->ac.mode <= 4 ? MODE_LABELS[ctx->ac.mode] : L"?")
        + L"  温度:" + (ctx->ac.temp >= 0 ? std::to_wstring(ctx->ac.temp) + L"℃" : L"?")
        + L"  风速:" + (ctx->ac.fan >= 0 && ctx->ac.fan <= 3 ? FAN_LABELS[ctx->ac.fan] : L"?")
        + L"  摆风:" + (ctx->ac.swing == 1 ? L"开" : (ctx->ac.swing == 0 ? L"关" : L"?"));
    SetWindowTextW(ctx->hAcStatus, s.c_str());
}

// ─── 构建图表序列 ───
void CDashboardDlg::BuildSeries(Ctx* ctx, std::vector<double>& powerYs,
                                std::vector<double>& energyYs, double& t0, double& t1,
                                double& todayKwh) {
    powerYs.clear(); energyYs.clear(); t0 = t1 = 0.0;
    todayKwh = ctx->plugin->GetTodayKwh();
    int win = ctx->winSec;
    std::vector<PowerSample> samples;
    if (win <= 600) samples = ctx->plugin->GetHistory().GetRecentSamples(win);
    else            samples = ctx->plugin->GetHistory().GetLongSamples((win + 3599) / 3600);
    if (samples.empty()) return;
    double cum = 0.0, prevT = samples[0].timestamp, prevW = samples[0].watts;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        powerYs.push_back(s.watts);
        double dt = s.timestamp - prevT; if (dt < 0) dt = 0;
        cum += (s.watts + prevW) * 0.5 * dt / 3600.0 / 1000.0;
        prevT = s.timestamp; prevW = s.watts;
        energyYs.push_back(cum);
    }
    t0 = samples.front().timestamp;
    t1 = samples.back().timestamp;
}

// ─── 绘制单张图（GDI+）───
void CDashboardDlg::DrawChart(HDC hdc, RECT rc, const std::vector<double>& ys,
                              double yMax, const wchar_t* title,
                              const wchar_t* unit, double curVal, COLORREF line,
                              double t0, double t1, int winSec) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // 卡片：填充背景（清除上一帧）+ 圆角边框
    int r = 10;
    GraphicsPath* card = RoundedRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, r);
    SolidBrush bgFill(C(g_theme.bg));
    g.FillPath(&bgFill, card);
    Pen borderPen(C(g_theme.cardBorder), 1.2f);
    g.DrawPath(&borderPen, card);
    delete card;

    int padL = 48, padR = 16, padT = 30, padB = 22;
    int plotW = (rc.right - rc.left) - padL - padR;
    int plotH = (rc.bottom - rc.top) - padT - padB;
    if (plotW < 20) plotW = 20;
    if (plotH < 20) plotH = 20;
    int px = rc.left + padL, py = rc.top + padT;
    int px2 = rc.right - padR, py2 = rc.bottom - padB;

    Font font(hdc, g_hFont);
    SolidBrush subBrush(C(g_theme.sub));
    StringFormat sf; sf.SetAlignment(StringAlignmentNear);

    // 标题
    g.DrawString(title, -1, &font, PointF((REAL)(rc.left + padL), (REAL)(rc.top + 6)), &subBrush);

    // 当前值徽章（右上）
    wchar_t buf[64];
    if (ys.empty()) swprintf_s(buf, L"— %ls", unit);
    else swprintf_s(buf, L"%.2f %ls", curVal, unit);
    Font fontV(hdc, g_hFont);
    RectF vbox; g.MeasureString(buf, -1, &fontV, PointF(0,0), &vbox);
    REAL bw = vbox.Width + 20, bh = 22;
    REAL bx = (REAL)(px2 - bw), by = (REAL)(rc.top + 6);
    GraphicsPath* pill = RoundedRect((int)bx, (int)by, (int)bw, (int)bh, 11);
    SolidBrush pillFill(C(line, 38));
    g.FillPath(&pillFill, pill);
    Pen pillPen(C(line, 120), 1.0f);
    g.DrawPath(&pillPen, pill);
    delete pill;
    SolidBrush valBrush(C(line));
    StringFormat vc; vc.SetAlignment(StringAlignmentCenter); vc.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(buf, -1, &fontV, RectF(bx, by, bw, bh), &vc, &valBrush);

    // 网格 + Y 轴刻度
    Pen gridPen(C(g_theme.grid), 1.0f);
    int gridN = 4;
    for (int i = 0; i <= gridN; ++i) {
        int yy = py + (int)(plotH * i / (double)gridN);
        g.DrawLine(&gridPen, px, yy, px2, yy);
        wchar_t lb[32]; swprintf_s(lb, L"%.1f", yMax * (gridN - i) / (double)gridN);
        g.DrawString(lb, -1, &font, PointF((REAL)(rc.left + 6), (REAL)(yy - 8)), &subBrush);
    }

    if (ys.empty()) {
        const wchar_t* tip = L"暂无数据（请先在选项中启用历史记录）";
        SolidBrush tb(C(g_theme.sub));
        StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(tip, -1, &font, RectF((REAL)px,(REAL)py,(REAL)plotW,(REAL)plotH), &cf, &tb);
        return;
    }

    // 折线点
    size_t n = ys.size();
    std::vector<PointF> pts(n);
    for (size_t i = 0; i < n; ++i) {
        double fx = (n == 1) ? 0.5 : (double)i / (n - 1);
        double v = ys[i]; if (yMax > 0 && v > yMax) v = yMax; if (v < 0) v = 0;
        pts[i].X = (REAL)(px + plotW * fx);
        pts[i].Y = (REAL)(py + plotH * (1.0 - (yMax > 0 ? v / yMax : 0)));
    }

    // 面积填充（渐变：线条色半透明 -> 透明）
    if (n >= 2) {
        GraphicsPath area;
        area.AddLines(pts.data(), (INT)n);
        area.AddLine(pts[n-1], PointF((REAL)px2, (REAL)py2));
        area.AddLine(PointF((REAL)px2, (REAL)py2), PointF((REAL)px, (REAL)py2));
        area.CloseFigure();
        LinearGradientBrush areaBrush(Point(0, py), Point(0, py2), C(line, 90), C(line, 8));
        g.FillPath(&areaBrush, &area);
    }

    // 折线（抗锯齿）
    Pen linePen(C(line), 2.2f);
    linePen.SetLineJoin(LineJoinRound);
    if (n >= 2) g.DrawLines(&linePen, pts.data(), (INT)n);

    // 末端高亮点（单点也画）
    SolidBrush dot(C(line));
    g.FillEllipse(&dot, pts[n-1].X - 3.5f, pts[n-1].Y - 3.5f, 7.0f, 7.0f);

    // X 轴时间/日期标签
    if (t1 > t0 && plotW > 60) {
        int xTicks = 5;
        StringFormat xf; xf.SetAlignment(StringAlignmentCenter);
        for (int i = 0; i <= xTicks; ++i) {
            double frac = (double)i / xTicks;
            time_t tt = (time_t)(t0 + (t1 - t0) * frac);
            struct tm tmv; localtime_s(&tmv, &tt);
            wchar_t lb[32];
            if (winSec <= 86400) swprintf_s(lb, L"%02d:%02d", tmv.tm_hour, tmv.tm_min);
            else                 swprintf_s(lb, L"%d/%d", tmv.tm_mon + 1, tmv.tm_mday);
            REAL xx = (REAL)(px + plotW * frac);
            RectF box(xx - 30, (REAL)(py2 + 4), 60, 16);
            g.DrawString(lb, -1, &font, box, &xf, &subBrush);
        }
    }
}

// ─── 绘制两张图 + 头部 ───
void CDashboardDlg::DrawCharts(HWND hWnd, Ctx* ctx) {
    UINT dpi = GetWindowDpi(hWnd);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    if (g_hFont) SelectObject(hdc, g_hFont);

    // 头部：标题 + 状态（GDI TextOutW，对 4K 高 DPI 混合中英文最稳；GDI+ DrawString 在 4K CJK+Latin 易重叠）
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_hFontTitle);
    SetTextColor(hdc, g_theme.text);
    TextOutW(hdc, S(20), S(12), L"米家空调伴侣", 6);
    SelectObject(hdc, g_hFont);
    SetTextColor(hdc, g_theme.sub);
    std::wstring st = L"设备：" + ConfigManager::Instance().Get().deviceName
        + (ctx->plugin->IsConnected() ? L"  ●已连接" : L"  ○未连接")
        + L"  " + std::to_wstring((int)ctx->plugin->GetCurrentWatts()) + L"W"
        + L"  今日" + std::to_wstring(ctx->plugin->GetTodayKwh()) + L"度";
    TextOutW(hdc, S(20), S(38), st.c_str(), (int)st.size());

    std::vector<double> powerYs, energyYs;
    double t0, t1, todayKwh;
    BuildSeries(ctx, powerYs, energyYs, t0, t1, todayKwh);

    // 右边界跟随窗口宽度（支持拉宽），左边界固定 S(20)
    RECT crc; GetClientRect(hWnd, &crc);
    int right = crc.right - S(20);
    RECT prc = { S(20), S(96),  right, S(280) };
    RECT erc = { S(20), S(290), right, S(474) };
    // AC 卡片边框
    RECT arc = { S(20), S(484), right, S(684) };

    double pMax = 0.0;
    for (double v : powerYs) if (v > pMax) pMax = v;
    if (pMax <= 0) pMax = 100.0; else pMax *= 1.2;
    double eMax = 0.0;
    for (double v : energyYs) if (v > eMax) eMax = v;
    if (eMax <= 0) eMax = (todayKwh > 0 ? todayKwh * 1.2 : 0.5); else eMax *= 1.2;

    const wchar_t* winName = ctx->winSec <= 600 ? L"近10分钟" :
        ctx->winSec <= 3600 ? L"近1小时" : ctx->winSec <= 86400 ? L"近24小时" : L"近7天";

    DrawChart(hdc, prc, powerYs, pMax, (std::wstring(L"功率  ") + winName).c_str(),
              L"W", ctx->plugin->GetCurrentWatts(), g_theme.powerLine, t0, t1, ctx->winSec);
    DrawChart(hdc, erc, energyYs, eMax, (std::wstring(L"电量  ") + winName).c_str(),
              L"度", energyYs.empty() ? 0.0 : energyYs.back(), g_theme.energyLine, t0, t1, ctx->winSec);

    // AC 控制卡片边框
    {
        Graphics gg(hdc);
        gg.SetSmoothingMode(SmoothingModeAntiAlias);
        GraphicsPath* p = RoundedRect(arc.left, arc.top, arc.right-arc.left, arc.bottom-arc.top, 12);
        Pen bp(C(g_theme.cardBorder), 1.2f);
        gg.DrawPath(&bp, p);
        delete p;
        // 分区标题
        Font f(hdc, g_hFont);
        SolidBrush sb(C(g_theme.sub));
        gg.DrawString(L"空调控制", -1, &f, PointF((REAL)(arc.left+16), (REAL)(arc.top+8)), &sb);
    }

    EndPaint(hWnd, &ps);
}

// ─── 自绘按钮 ───
static void DrawButton(LPDRAWITEMSTRUCT dis) {
    if (!dis) return;
    HDC hdc = dis->hDC; RECT rc = dis->rcItem;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    wchar_t text[64]; GetWindowTextW(dis->hwndItem, text, 64);
    bool active = wcsstr(text, L"●") != nullptr;        // 时间范围选中
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF fill = active ? g_theme.btnActive : (pressed ? g_theme.btnHover : g_theme.btnFace);
    if (disabled) fill = g_theme.bg;
    int r = 8;
    GraphicsPath* p = RoundedRect(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, r);
    SolidBrush fb(C(fill));
    g.FillPath(&fb, p);
    if (!active) { Pen bp(C(g_theme.cardBorder), 1.0f); g.DrawPath(&bp, p); }
    delete p;

    Font font(hdc, (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
    Color tc = active ? Color(255,255,255) : (disabled ? C(g_theme.sub) : C(g_theme.btnText));
    SolidBrush tb(tc);
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
    RectF rf((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right-rc.left), (REAL)(rc.bottom-rc.top));
    g.DrawString(text, -1, &font, rf, &sf, &tb);
}

// ─── 定时器 ───
void CDashboardDlg::OnTimer(HWND hWnd, Ctx* ctx) {
    // 重绘图表区（跟随窗口宽度）
    (void)ctx;
    RECT crc; GetClientRect(hWnd, &crc);
    InvalidateRect(hWnd, &crc, FALSE);
}

// ─── 窗口过程 ───
LRESULT CALLBACK CDashboardDlg::DlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Ctx* ctx = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ctx = (Ctx*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);
        // GDI+ 启动
        GdiplusStartupInput si; ULONG_PTR tok = 0;
        GdiplusStartup(&tok, &si, NULL);
        SetPropW(hWnd, L"GTOK", (HANDLE)tok);
        OnInitDialog(hWnd, ctx);
        if (ctx->acEnabled) {
            ctx->plugin->AcRefreshState(ctx->ac);
            int pw = 0; if (ctx->plugin->AcGetPowerState(pw)) ctx->ac.power = pw;
            UpdateAcUi(ctx);
        }
        return 0;
    }
    ctx = (Ctx*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hb = CreateSolidBrush(g_theme.bg);
        FillRect(hdc, &rc, hb); DeleteObject(hb);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_theme.text);
        SetBkColor(hdc, g_theme.bg);
        return (LRESULT)(g_hbrBg ? g_hbrBg : GetStockObject(HOLLOW_BRUSH));
    }
    case WM_DRAWITEM:
        DrawButton((LPDRAWITEMSTRUCT)lParam);
        return TRUE;
    case WM_COMMAND: {
        if (!ctx) break;
        int id = LOWORD(wParam);
        if (id == IDC_BTN_10M || id == IDC_BTN_1H || id == IDC_BTN_24H || id == IDC_BTN_7D) {
            int secs[4] = { 600, 3600, 86400, 604800 };
            int idx = (id == IDC_BTN_10M) ? 0 : (id == IDC_BTN_1H) ? 1 : (id == IDC_BTN_24H) ? 2 : 3;
            ctx->winSec = secs[idx];
            int ids[4] = { IDC_BTN_10M, IDC_BTN_1H, IDC_BTN_24H, IDC_BTN_7D };
            const wchar_t* names[4] = { L"10分钟", L"1小时", L"24小时", L"7天" };
            for (int i = 0; i < 4; i++) {
                wchar_t buf[16];
                swprintf_s(buf, idx == i ? L"●%s" : L"%s", names[i]);
                SetWindowTextW(GetDlgItem(hWnd, ids[i]), buf);
            }
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (id == IDC_BTN_POWER) {
            bool on = (ctx->ac.power != 1);
            if (ctx->plugin->AcSetPower(on, ctx->ac))
                SetWindowTextW(ctx->hErr, on ? L"已发送开机指令" : L"已发送关机指令");
            else
                SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_BTN_TDN) {
            int t = ctx->ac.temp; if (t < 0) t = 26; if (t > 16) t--;
            if (ctx->plugin->AcSetTemp(t, ctx->ac)) SetWindowTextW(ctx->hErr, L"温度已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_BTN_TUP) {
            int t = ctx->ac.temp; if (t < 0) t = 26; if (t < 30) t++;
            if (ctx->plugin->AcSetTemp(t, ctx->ac)) SetWindowTextW(ctx->hErr, L"温度已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_BTN_SWING) {
            int s = (ctx->ac.swing == 1) ? 0 : 1;
            if (ctx->plugin->AcSetSwing(s, ctx->ac)) SetWindowTextW(ctx->hErr, L"摆风已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_COMBO_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            int m = (int)SendMessageW(ctx->hMode, CB_GETCURSEL, 0, 0);
            if (ctx->plugin->AcSetMode(m, ctx->ac)) SetWindowTextW(ctx->hErr, L"模式已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_COMBO_FAN && HIWORD(wParam) == CBN_SELCHANGE) {
            int f = (int)SendMessageW(ctx->hFan, CB_GETCURSEL, 0, 0);
            if (ctx->plugin->AcSetFan(f, ctx->ac)) SetWindowTextW(ctx->hErr, L"风速已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            UpdateAcUi(ctx);
        } else if (id == IDC_BTN_REFRESH) {
            RefreshAc(ctx);
            SetWindowTextW(ctx->hErr, ctx->ac.lastError.empty() ? L"已刷新" : ctx->ac.lastError.c_str());
        } else if (id == IDC_BTN_DETECT) {
            std::wstring model;
            if (ctx->plugin->AcDetectModel(model))
                SetWindowTextW(ctx->hErr, (L"状态：" + model).c_str());
            else
                SetWindowTextW(ctx->hErr, L"探测失败：设备未连接或通信错误");
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (id == IDC_BTN_TEST) {
            AcState t;
            bool ok = ctx->plugin->AcRefreshState(t);
            if (ok && t.lastCode == 0)
                SetWindowTextW(ctx->hErr, L"通信正常，状态读取成功");
            else if (!t.lastError.empty())
                SetWindowTextW(ctx->hErr, t.lastError.c_str());
            else
                SetWindowTextW(ctx->hErr, L"测试失败");
            UpdateAcUi(ctx);
        }
        break;
    }
    case WM_TIMER:
        if (ctx) OnTimer(hWnd, ctx);
        break;
    case WM_SIZE:
        InvalidateRect(hWnd, NULL, FALSE);   // 窗口大小变化时重绘，图表跟随宽度
        break;
    case WM_PAINT:
        if (ctx) DrawCharts(hWnd, ctx);
        else { PAINTSTRUCT ps; BeginPaint(hWnd,&ps); EndPaint(hWnd,&ps); }
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        ULONG_PTR tok = (ULONG_PTR)GetPropW(hWnd, L"GTOK");
        if (tok) GdiplusShutdown(tok);
        RemovePropW(hWnd, L"GTOK");
        g_hDashboard = nullptr;
        delete ctx;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
