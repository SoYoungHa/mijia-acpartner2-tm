// DashboardDlg.cpp - 图表 + 空调控制面板实现（纯Win32，无RC文件）
#include "pch.h"
#include "DashboardDlg.h"
#include "MijiaPowerPlugin.h"
#include "MiioDevice.h"
#include <commctrl.h>
#include <string>
#include <vector>
#include <cmath>

// ─── 控件 ID ───
#define IDC_STATUS       2001
#define IDC_BTN_10M      2010
#define IDC_BTN_1H       2011
#define IDC_BTN_24H      2012
#define IDC_BTN_7D       2013

#define IDC_GROUP_AC     2014
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
#define IDC_CHECK_AC     2032

static const wchar_t* MODE_LABELS[] = { L"自动", L"制冷", L"除湿", L"送风", L"制热" };
static const wchar_t* FAN_LABELS[]  = { L"自动", L"低", L"中", L"高" };

// ─── 单实例句柄 ───
static HWND g_hDashboard = nullptr;

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

// ─── 显示 / 激活 ───
void CDashboardDlg::Show(HWND hParent, CMijiaPowerPlugin* plugin) {
    if (!plugin) return;
    if (g_hDashboard && IsWindow(g_hDashboard)) {
        SetForegroundWindow(g_hDashboard);
        return;
    }
    HINSTANCE hInst = hParent ? (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE)
                              : GetModuleHandleW(NULL);

    // 注册窗口类（只一次）
    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MijiaDashboardWnd";
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        atom = RegisterClassExW(&wc);
    }

    UINT dpi = GetWindowDpi(hParent ? hParent : nullptr);
    int W = MulDiv(820, dpi, 96);
    int H = MulDiv(700, dpi, 96);

    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (hParent) {
        RECT rc{}; GetWindowRect(hParent, &rc);
        px = rc.left + (rc.right - rc.left - W) / 2;
        py = rc.top + (rc.bottom - rc.top - H) / 2;
        if (px < 0) px = 0; if (py < 0) py = 0;
    }

    Ctx* ctx = new Ctx();
    ctx->plugin = plugin;

    HWND hWnd = CreateWindowExW(
        0,
        L"MijiaDashboardWnd",
        L"米家空调伴侣 - 图表与控制",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        px, py, W, H,
        hParent, NULL, hInst,
        ctx   // 传给 WM_CREATE
    );
    if (!hWnd) { delete ctx; return; }
    g_hDashboard = hWnd;
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
}

// ─── 初始化控件 ───
void CDashboardDlg::OnInitDialog(HWND hWnd, Ctx* ctx) {
    UINT dpi = GetWindowDpi(hWnd);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(14, dpi, 96);
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"微软雅黑");
    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    ctx->acEnabled = false; // acEnabled 在下方根据配置设置
    // 存字体到 ctx 以便 WM_PAINT 使用
    SetPropW(hWnd, L"MFONT", hFont);

    // 顶部状态
    ctx->hStatus = AddCtrl(hWnd, L"STATIC", L"", SS_LEFT, S(16), S(12), S(786), S(30),
                           IDC_STATUS, hFont);

    // 时间范围按钮
    AddCtrl(hWnd, L"BUTTON", L"10分钟", BS_PUSHBUTTON, S(16),  S(244), S(90),  S(26), IDC_BTN_10M, hFont);
    AddCtrl(hWnd, L"BUTTON", L"●1小时", BS_PUSHBUTTON, S(114), S(244), S(90),  S(26), IDC_BTN_1H,  hFont);
    AddCtrl(hWnd, L"BUTTON", L"24小时", BS_PUSHBUTTON, S(212), S(244), S(90),  S(26), IDC_BTN_24H, hFont);
    AddCtrl(hWnd, L"BUTTON", L"7天",    BS_PUSHBUTTON, S(310), S(244), S(90),  S(26), IDC_BTN_7D,  hFont);

    // 空调控制分组
    AddCtrl(hWnd, L"BUTTON", L"空调控制（米家空调伴侣）", BS_GROUPBOX, S(16), S(478), S(786), S(150),
            IDC_GROUP_AC, hFont);

    ctx->acEnabled = ConfigManager::Instance().Get().enableAcControl;

    ctx->hPower = AddCtrl(hWnd, L"BUTTON", L"开机", BS_PUSHBUTTON, S(30), S(500), S(120), S(30),
                          IDC_BTN_POWER, hFont);
    AddCtrl(hWnd, L"BUTTON", L"−", BS_PUSHBUTTON, S(160), S(500), S(32), S(30), IDC_BTN_TDN, hFont);
    ctx->hTemp  = AddCtrl(hWnd, L"STATIC", L"--℃", SS_CENTER | SS_CENTERIMAGE, S(196), S(500), S(70), S(30),
                          IDC_STATIC_TEMP, hFont);
    AddCtrl(hWnd, L"BUTTON", L"+", BS_PUSHBUTTON, S(270), S(500), S(32), S(30), IDC_BTN_TUP, hFont);

    AddCtrl(hWnd, L"STATIC", L"模式:", 0, S(320), S(500), S(50), S(30), 0, hFont);
    ctx->hMode  = AddCtrl(hWnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, S(374), S(500), S(120), S(160),
                          IDC_COMBO_MODE, hFont);
    for (int i = 0; i < 5; i++) SendMessageW(ctx->hMode, CB_ADDSTRING, 0, (LPARAM)MODE_LABELS[i]);

    AddCtrl(hWnd, L"STATIC", L"风速:", 0, S(504), S(500), S(50), S(30), 0, hFont);
    ctx->hFan   = AddCtrl(hWnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST, S(558), S(500), S(110), S(140),
                          IDC_COMBO_FAN, hFont);
    for (int i = 0; i < 4; i++) SendMessageW(ctx->hFan, CB_ADDSTRING, 0, (LPARAM)FAN_LABELS[i]);

    ctx->hSwing = AddCtrl(hWnd, L"BUTTON", L"摆风: 关", BS_PUSHBUTTON, S(678), S(500), S(110), S(30),
                          IDC_BTN_SWING, hFont);

    AddCtrl(hWnd, L"BUTTON", L"刷新",   BS_PUSHBUTTON, S(30),  S(596), S(90),  S(26), IDC_BTN_REFRESH, hFont);
    AddCtrl(hWnd, L"BUTTON", L"探测型号", BS_PUSHBUTTON, S(128), S(596), S(110), S(26), IDC_BTN_DETECT, hFont);
    AddCtrl(hWnd, L"BUTTON", L"测试指令", BS_PUSHBUTTON, S(246), S(596), S(90),  S(26), IDC_BTN_TEST,   hFont);

    ctx->hAcStatus = AddCtrl(hWnd, L"STATIC", L"", SS_LEFT, S(350), S(596), S(440), S(26), IDC_STATIC_AC, hFont);
    ctx->hErr      = AddCtrl(hWnd, L"STATIC", L"", SS_LEFT, S(16),  S(626), S(780), S(20), IDC_STATIC_ERR, hFont);

    if (!ctx->acEnabled) {
        // 未启用：禁用控制控件并提示
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
    // 同时读取开关状态（旧协议）
    int pw = 0;
    if (ctx->plugin->AcGetPowerState(pw)) ctx->ac.power = pw;
    UpdateAcUi(ctx);
}

void CDashboardDlg::UpdateAcUi(Ctx* ctx) {
    // 电源按钮
    if (ctx->ac.power == 1) SetWindowTextW(ctx->hPower, L"关机");
    else if (ctx->ac.power == 0) SetWindowTextW(ctx->hPower, L"开机");
    else SetWindowTextW(ctx->hPower, L"电源?");

    // 温度
    if (ctx->ac.temp >= 0) {
        wchar_t buf[32]; swprintf_s(buf, L"%d℃", ctx->ac.temp);
        SetWindowTextW(ctx->hTemp, buf);
    } else SetWindowTextW(ctx->hTemp, L"--℃");

    // 模式 / 风速
    if (ctx->ac.mode >= 0 && ctx->ac.mode <= 4)
        SendMessageW(ctx->hMode, CB_SETCURSEL, ctx->ac.mode, 0);
    if (ctx->ac.fan >= 0 && ctx->ac.fan <= 3)
        SendMessageW(ctx->hFan, CB_SETCURSEL, ctx->ac.fan, 0);

    // 摆风
    if (ctx->ac.swing == 1) SetWindowTextW(ctx->hSwing, L"摆风: 开");
    else if (ctx->ac.swing == 0) SetWindowTextW(ctx->hSwing, L"摆风: 关");
    else SetWindowTextW(ctx->hSwing, L"摆风: ?");

    // 状态文字
    std::wstring s = L"电源:" + std::wstring(ctx->ac.power == 1 ? L"开" : (ctx->ac.power == 0 ? L"关" : L"?"))
        + L"  模式:" + (ctx->ac.mode >= 0 && ctx->ac.mode <= 4 ? MODE_LABELS[ctx->ac.mode] : L"?")
        + L"  温度:" + (ctx->ac.temp >= 0 ? std::to_wstring(ctx->ac.temp) + L"℃" : L"?")
        + L"  风速:" + (ctx->ac.fan >= 0 && ctx->ac.fan <= 3 ? FAN_LABELS[ctx->ac.fan] : L"?")
        + L"  摆风:" + (ctx->ac.swing == 1 ? L"开" : (ctx->ac.swing == 0 ? L"关" : L"?"));
    SetWindowTextW(ctx->hAcStatus, s.c_str());

    if (!ctx->ac.lastError.empty())
        SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
}

// ─── 构建图表序列 ───
void CDashboardDlg::BuildSeries(Ctx* ctx, std::vector<double>& powerYs,
                                std::vector<double>& energyYs, double& t0, double& t1,
                                double& todayKwh) {
    powerYs.clear(); energyYs.clear(); t0 = t1 = 0.0;
    todayKwh = ctx->plugin->GetTodayKwh();

    int win = ctx->winSec;
    std::vector<PowerSample> samples;
    if (win <= 600)
        samples = ctx->plugin->GetHistory().GetRecentSamples(win);
    else
        samples = ctx->plugin->GetHistory().GetLongSamples((win + 3599) / 3600);

    if (samples.empty()) return;

    double cum = 0.0, prevT = samples[0].timestamp, prevW = samples[0].watts;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        powerYs.push_back(s.watts);
        double dt = s.timestamp - prevT; if (dt < 0) dt = 0;
        // 梯形积分（kWh）
        cum += (s.watts + prevW) * 0.5 * dt / 3600.0 / 1000.0;
        prevT = s.timestamp; prevW = s.watts;
        energyYs.push_back(cum);
    }
    t0 = samples.front().timestamp;
    t1 = samples.back().timestamp;
}

// ─── 绘制单张图 ───
void CDashboardDlg::DrawChart(HDC hdc, RECT rc, const std::vector<double>& ys,
                              double yMax, const wchar_t* title,
                              const wchar_t* unit, double curVal, COLORREF line) {
    // 背景
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg); DeleteObject(bg);
    // 边框
    FrameRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    int padL = 46, padR = 12, padT = 22, padB = 22;
    int plotW = rc.right - rc.left - padL - padR;
    int plotH = rc.bottom - rc.top - padT - padB;
    if (plotW < 10) plotW = 10;
    if (plotH < 10) plotH = 10;

    RECT prc = { rc.left + padL, rc.top + padT, rc.right - padR, rc.bottom - padB };

    // 网格 + Y 轴刻度
    int gridN = 4;
    for (int i = 0; i <= gridN; ++i) {
        int y = prc.top + (int)(plotH * i / (double)gridN);
        MoveToEx(hdc, prc.left, y, NULL); LineTo(hdc, prc.right, y);
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", yMax * (gridN - i) / (double)gridN);
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, rc.left + 4, y - 8, buf, (int)wcslen(buf));
    }

    // 标题
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, rc.left + padL, rc.top + 2, title, (int)wcslen(title));

    if (ys.empty()) {
        const wchar_t* tip = L"暂无数据（请先在选项中启用历史记录）";
        TextOutW(hdc, prc.left + 10, prc.top + plotH / 2, tip, (int)wcslen(tip));
        return;
    }

    // 折线
    HPEN hPen = CreatePen(PS_SOLID, 2, line);
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    size_t n = ys.size();
    POINT* pts = new POINT[n];
    for (size_t i = 0; i < n; ++i) {
        double fx = (n == 1) ? 0.5 : (double)i / (n - 1);
        double v = ys[i]; if (yMax > 0 && v > yMax) v = yMax; if (v < 0) v = 0;
        pts[i].x = prc.left + (int)(plotW * fx);
        pts[i].y = prc.top + (int)(plotH * (1.0 - (yMax > 0 ? v / yMax : 0)));
    }
    Polyline(hdc, pts, (int)n);
    delete[] pts;
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    // 当前值
    wchar_t buf[64];
    swprintf_s(buf, L"当前 %.2f %s", curVal, unit);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, prc.right - 150, rc.top + 2, buf, (int)wcslen(buf));
}

// ─── 绘制两张图 + 标题 ───
void CDashboardDlg::DrawCharts(HWND hWnd, Ctx* ctx) {
    UINT dpi = GetWindowDpi(hWnd);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    HFONT hFont = (HFONT)GetPropW(hWnd, L"MFONT");
    if (hFont) SelectObject(hdc, hFont);

    // 状态栏文字
    if (ctx->hStatus) {
        std::wstring st = L"设备：" + ConfigManager::Instance().Get().deviceName;
        if (!ConfigManager::Instance().Get().acModel.empty())
            st += L"  型号：" + ConfigManager::Instance().Get().acModel;
        st += ctx->plugin->IsConnected() ? L"  已连接" : L"  未连接";
        st += L"  当前功率：" + std::to_wstring((int)ctx->plugin->GetCurrentWatts()) + L"W";
        st += L"  今日用电：" + std::to_wstring(ctx->plugin->GetTodayKwh()) + L"度";
        SetWindowTextW(ctx->hStatus, st.c_str());
    }

    // 计算序列
    std::vector<double> powerYs, energyYs;
    double t0, t1, todayKwh;
    BuildSeries(ctx, powerYs, energyYs, t0, t1, todayKwh);

    RECT prc = { S(CHART_X), S(POWER_Y), S(CHART_X + CHART_W), S(POWER_Y + POWER_H) };
    RECT erc = { S(CHART_X), S(ENERGY_Y), S(CHART_X + CHART_W), S(ENERGY_Y + ENERGY_H) };

    // Y 轴量程
    double pMax = 0.0;
    for (double v : powerYs) if (v > pMax) pMax = v;
    if (pMax <= 0) pMax = 100.0; else pMax *= 1.2;
    double eMax = 0.0;
    for (double v : energyYs) if (v > eMax) eMax = v;
    if (eMax <= 0) eMax = (todayKwh > 0 ? todayKwh * 1.2 : 0.5);
    else eMax *= 1.2;

    const wchar_t* winName = ctx->winSec <= 600 ? L"近10分钟" :
        ctx->winSec <= 3600 ? L"近1小时" : ctx->winSec <= 86400 ? L"近24小时" : L"近7天";

    DrawChart(hdc, prc, powerYs, pMax, (std::wstring(L"功率图（") + winName + L"） W").c_str(),
              L"W", ctx->plugin->GetCurrentWatts(), RGB(0, 120, 215));
    DrawChart(hdc, erc, energyYs, eMax, (std::wstring(L"电量图（") + winName + L"） 度").c_str(),
              L"度", energyYs.empty() ? 0.0 : energyYs.back(), RGB(220, 90, 40));

    EndPaint(hWnd, &ps);
}

// ─── 定时器 ───
void CDashboardDlg::OnTimer(HWND hWnd, Ctx* ctx) {
    // 重绘图表
    UINT dpi = GetWindowDpi(hWnd);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    RECT prc = { S(CHART_X), S(POWER_Y), S(CHART_X + CHART_W), S(POWER_Y + POWER_H) };
    RECT erc = { S(CHART_X), S(ENERGY_Y), S(CHART_X + CHART_W), S(ENERGY_Y + ENERGY_H) };
    InvalidateRect(hWnd, &prc, TRUE);
    InvalidateRect(hWnd, &erc, TRUE);

    // 每 5 次（约10秒）自动刷新空调状态
    if (ctx->acEnabled) {
        ctx->acCnt++;
        if (ctx->acCnt >= 5) { ctx->acCnt = 0; RefreshAc(ctx); }
    }
}

// ─── 窗口过程 ───
LRESULT CALLBACK CDashboardDlg::DlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Ctx* ctx = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ctx = (Ctx*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);
        OnInitDialog(hWnd, ctx);
        // 初次读取空调状态
        if (ctx->acEnabled) {
            ctx->plugin->AcRefreshState(ctx->ac);
            int pw = 0; if (ctx->plugin->AcGetPowerState(pw)) ctx->ac.power = pw;
            UpdateAcUi(ctx);
        }
        return 0;
    }
    ctx = (Ctx*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND: {
        if (!ctx) break;
        int id = LOWORD(wParam);
        if (id == IDC_BTN_10M || id == IDC_BTN_1H || id == IDC_BTN_24H || id == IDC_BTN_7D) {
            int secs[4] = { 600, 3600, 86400, 604800 };
            int idx = (id == IDC_BTN_10M) ? 0 : (id == IDC_BTN_1H) ? 1 : (id == IDC_BTN_24H) ? 2 : 3;
            ctx->winSec = secs[idx];
            // 高亮选中
            int ids[4] = { IDC_BTN_10M, IDC_BTN_1H, IDC_BTN_24H, IDC_BTN_7D };
            const wchar_t* names[4] = { L"10分钟", L"1小时", L"24小时", L"7天" };
            for (int i = 0; i < 4; i++) {
                wchar_t buf[16];
                swprintf_s(buf, idx == i ? L"●%s" : L"%s", names[i]);
                SetWindowTextW(GetDlgItem(hWnd, ids[i]), buf);
            }
            InvalidateRect(hWnd, NULL, TRUE);
        } else if (id == IDC_BTN_POWER) {
            bool on = (ctx->ac.power != 1);
            if (ctx->plugin->AcSetPower(on, ctx->ac))
                SetWindowTextW(ctx->hErr, on ? L"已发送开机指令" : L"已发送关机指令");
            else
                SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_BTN_TDN) {
            int t = ctx->ac.temp; if (t < 0) t = 26; if (t > 16) t--;
            if (ctx->plugin->AcSetTemp(t, ctx->ac)) SetWindowTextW(ctx->hErr, L"温度已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_BTN_TUP) {
            int t = ctx->ac.temp; if (t < 0) t = 26; if (t < 30) t++;
            if (ctx->plugin->AcSetTemp(t, ctx->ac)) SetWindowTextW(ctx->hErr, L"温度已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_BTN_SWING) {
            int s = (ctx->ac.swing == 1) ? 0 : 1;
            if (ctx->plugin->AcSetSwing(s, ctx->ac)) SetWindowTextW(ctx->hErr, L"摆风已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_COMBO_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            int m = (int)SendMessageW(ctx->hMode, CB_GETCURSEL, 0, 0);
            if (ctx->plugin->AcSetMode(m, ctx->ac)) SetWindowTextW(ctx->hErr, L"模式已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_COMBO_FAN && HIWORD(wParam) == CBN_SELCHANGE) {
            int f = (int)SendMessageW(ctx->hFan, CB_GETCURSEL, 0, 0);
            if (ctx->plugin->AcSetFan(f, ctx->ac)) SetWindowTextW(ctx->hErr, L"风速已设置");
            else SetWindowTextW(ctx->hErr, ctx->ac.lastError.c_str());
            RefreshAc(ctx);
        } else if (id == IDC_BTN_REFRESH) {
            RefreshAc(ctx);
            SetWindowTextW(ctx->hErr, ctx->ac.lastError.empty() ? L"已刷新" : ctx->ac.lastError.c_str());
        } else if (id == IDC_BTN_DETECT) {
            std::wstring model;
            if (ctx->plugin->AcDetectModel(model))
                SetWindowTextW(ctx->hErr, (L"探测到型号：" + model).c_str());
            else
                SetWindowTextW(ctx->hErr, L"探测失败：设备未连接或通信错误");
            InvalidateRect(hWnd, NULL, TRUE);
        } else if (id == IDC_BTN_TEST) {
            AcState t;
            bool ok = ctx->plugin->AcRefreshState(t);
            if (ok && t.lastCode == 0)
                SetWindowTextW(ctx->hErr, L"指令映射正常（siid/piid 正确）");
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
    case WM_PAINT:
        if (ctx) DrawCharts(hWnd, ctx);
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        g_hDashboard = nullptr;
        delete ctx;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
