// MijiaPowerPlugin.h - 插件主类声明
#pragma once
#include "pch.h"
#include "PluginInterface.h"
#include "MiioDevice.h"
#include "PowerHistory.h"
#include "PluginConfig.h"

// ─────────────────────────────────────────────
// 功率显示项（主显示：实时功率）
// ─────────────────────────────────────────────
class CPowerItem : public IPluginItem {
public:
    void SetPlugin(class CMijiaPowerPlugin* plugin) { m_plugin = plugin; }

    const wchar_t* GetItemName()            const override;
    const wchar_t* GetItemId()              const override { return L"MijiaPowerW"; }
    const wchar_t* GetItemLableText()       const override;
    const wchar_t* GetItemValueText()       const override;
    const wchar_t* GetItemValueSampleText() const override { return L"9999.9W"; }

    // 在任务栏内联绘制功率迷你图（基类返回 int：0 不绘 / 非0 绘制）
    int IsDrawResourceUsageGraph() const override { return 1; }
    float GetResourceUsageGraphValue() const override;

    // 双击打开图表/控制面板
    int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

private:
    class CMijiaPowerPlugin* m_plugin = nullptr;
    mutable std::wstring m_valueText;
    mutable std::wstring m_labelText;
};

// ─────────────────────────────────────────────
// 今日用电量显示项（本地按功率积分估算）
// ─────────────────────────────────────────────
class CEnergyItem : public IPluginItem {
public:
    void SetPlugin(class CMijiaPowerPlugin* plugin) { m_plugin = plugin; }

    const wchar_t* GetItemName()            const override;
    const wchar_t* GetItemId()              const override { return L"MijiaPowerKwh"; }
    const wchar_t* GetItemLableText()       const override;
    const wchar_t* GetItemValueText()       const override;
    const wchar_t* GetItemValueSampleText() const override { return L"9.99度"; }

private:
    class CMijiaPowerPlugin* m_plugin = nullptr;
    mutable std::wstring m_valueText;
    mutable std::wstring m_labelText;
};

// ─────────────────────────────────────────────
// 空调状态（供控制面板使用）
// ─────────────────────────────────────────────
struct AcState {
    int power = -1;          // 0 关 / 1 开 / -1 未知
    int mode  = -1;          // 0 自动 1 制冷 2 除湿 3 送风 4 制热
    int temp  = -1;          // 16..30 ℃
    int fan   = -1;          // 0 自动 1 低 2 中 3 高
    int swing = -1;          // 0 关 / 1 开
    int lastCode = 0;        // 最近一次指令返回码（0 成功）
    std::wstring lastError;  // 最近一次错误信息
};

// ─────────────────────────────────────────────
// 插件主类
// ─────────────────────────────────────────────
class CMijiaPowerPlugin : public ITMPlugin {
public:
    CMijiaPowerPlugin();
    ~CMijiaPowerPlugin();

    // ── ITMPlugin 接口实现 ──
    IPluginItem*   GetItem(int index)  override;
    int            GetItemCount() const { return 2; }
    void           DataRequired()      override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;

    // 返回值改为 OptionReturn（官方接口）
    OptionReturn   ShowOptionsDialog(void* hParent) override;

    const wchar_t* GetTooltipInfo()    override;

    // API v7: 主程序调用此函数完成初始化，传入 ITrafficMonitor*
    void           OnInitialize(ITrafficMonitor* pApp) override;

    // 配置目录通过 OnExtenedInfo(EI_CONFIG_DIR, ...) 传入
    void           OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    // 插件命令（右键菜单）
    int            GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void           OnPluginCommand(int command_index, void* hWnd, void* para) override;

    // 安全关闭（DllMain DLL_PROCESS_DETACH 阶段调用，detach 线程避免死锁）
    void Shutdown() {
        m_stopFlag = true;
        if (m_sampleThread.joinable())
            m_sampleThread.detach();
    }

    // 供 CPowerItem / CEnergyItem 访问
    double         GetCurrentWatts()   const;
    double         GetTodayKwh()       const { return m_todayWh.load() / 1000.0; }
    bool           IsConnected()       const { return m_connected.load(); }
    const PowerHistory& GetHistory()   const { return m_history; }

    // ── 空调控制 API（供 DashboardDlg 调用）──
    bool AcEnsureConnected();                 // 必要时重连（加锁）
    bool AcEnsureConnectedLocked();           // 同上，但假定已持有 m_deviceMutex
    bool AcSetPower(bool on, AcState& out);   // 旧协议开关机
    bool AcGetPowerState(int& out);           // 读取开关状态（0/1）
    bool AcRefreshState(AcState& out);        // SPEC 读取 模式/温度/风速/摆风
    bool AcSetTemp(int t, AcState& out);      // 设置目标温度
    bool AcSetMode(int m, AcState& out);      // 设置模式
    bool AcSetFan(int f, AcState& out);       // 设置风速
    bool AcSetSwing(int s, AcState& out);     // 设置摆风
    bool AcDetectModel(std::wstring& out);    // 探测设备 model
    void OpenDashboard(HWND hParent);         // 打开图表+控制面板

private:
    ITrafficMonitor*   m_pTM = nullptr;
    CPowerItem         m_powerItem;
    CEnergyItem        m_energyItem;
    PowerHistory       m_history;

    // 后台采集线程
    std::thread        m_sampleThread;
    std::atomic<bool>  m_stopFlag{ false };
    std::atomic<bool>  m_connected{ false };
    std::atomic<double> m_currentWatts{ 0.0 };

    // 今日用电量（本地按功率积分）
    std::atomic<double> m_todayWh{ 0.0 };
    std::wstring        m_energyDate;
    int                 m_persistCnt = 0;

    // tooltip缓存
    mutable std::wstring m_tooltipText;

    bool               m_initialized = false;  // 防止重复初始化

    void StartSampling();  // 启动采样线程（在配置目录确定后调用）
    void SampleLoop();
    void ConnectDevice();
    void DisconnectDevice();
    void InitEnergy();     // 从配置恢复今日用电，跨天则清零
    void UpdateEnergy(double watts, int intervalSec);  // 累积并定期落盘
    static std::wstring TodayStr();

    // 在已持有 m_deviceMutex 时调用，发送 SPEC 读
    bool DeviceGetProperties(const std::vector<MiioProperty>& props, std::string& outResult);
    bool DeviceSetProperties(const std::vector<MiioPropValue>& vals,  std::string& outResult);
    // 解析 SPEC 结果并返回错误码（-1 表示解析/通信失败）
    static int ParseFirstCode(const std::string& result);

    std::unique_ptr<MiioDevice> m_device;
    mutable std::mutex          m_deviceMutex;
};

// 供 OptionsDlg 等获取插件单例
CMijiaPowerPlugin* MijiaPluginInstance();

// DLL 导出
extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance();
