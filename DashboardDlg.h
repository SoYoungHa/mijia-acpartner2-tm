// DashboardDlg.h - 图表 + 空调控制面板（纯Win32，无RC文件）
#pragma once
#include "pch.h"
#include "resource.h"
#include "MijiaPowerPlugin.h"   // AcState / CMijiaPowerPlugin 完整定义

class CDashboardDlg {
public:
    // 显示/激活面板（单实例）。hParent 为 TrafficMonitor 传入的窗口句柄。
    static void Show(HWND hParent, CMijiaPowerPlugin* plugin);

private:
    struct Ctx {
        CMijiaPowerPlugin* plugin = nullptr;
        AcState             ac;
        int                 winSec = 3600;   // 当前图表时间窗（秒）
        int                 acCnt  = 0;      // AC 自动刷新计数
        bool                acEnabled = false;

        // 控件句柄
        HWND hStatus = nullptr;
        HWND hAcStatus = nullptr;
        HWND hErr = nullptr;
        HWND hTemp = nullptr;
        HWND hPower = nullptr;
        HWND hMode = nullptr;
        HWND hFan = nullptr;
        HWND hSwing = nullptr;
    };

    static LRESULT CALLBACK DlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void OnInitDialog(HWND hWnd, Ctx* ctx);
    static void OnTimer(HWND hWnd, Ctx* ctx);
    static void RefreshAc(Ctx* ctx);                 // 读取空调状态 -> 更新 UI
    static void UpdateAcUi(Ctx* ctx);                // 根据 m_ac 刷新控件文字
    static void DrawCharts(HWND hWnd, Ctx* ctx);     // 重绘两张图
    static void DrawChart(HDC hdc, RECT rc, const std::vector<double>& ys,
                          double yMax, const wchar_t* title,
                          const wchar_t* unit, double curVal, COLORREF line,
                          double t0, double t1, int winSec,
                          const wchar_t* yFmt = L"%.1f");
    static void BuildSeries(Ctx* ctx, std::vector<double>& powerYs,
                            std::vector<double>& energyYs,
                            double& t0, double& t1, double& todayKwh);

    // 图表区域（相对窗口客户区，逻辑坐标；绘制时按 DPI 缩放）
    static const int CHART_X = 16, CHART_W = 786;
    static const int POWER_Y = 56,  POWER_H = 178;
    static const int ENERGY_Y = 286, ENERGY_H = 178;
};
