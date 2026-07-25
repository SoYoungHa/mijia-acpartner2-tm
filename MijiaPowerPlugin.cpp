// MijiaPowerPlugin.cpp - 插件主类实现
#include "pch.h"
#include "MijiaPowerPlugin.h"
#include "OptionsDlg.h"
#include "DashboardDlg.h"
#include <sstream>
#include <iomanip>

// ═══════════════════════════════════════════════
// DLL 导出入口
// ═══════════════════════════════════════════════
static CMijiaPowerPlugin* g_pluginInstance = nullptr;

extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance() {
    if (!g_pluginInstance) {
        g_pluginInstance = new CMijiaPowerPlugin();
    }
    return g_pluginInstance;
}

CMijiaPowerPlugin* MijiaPluginInstance() {
    return g_pluginInstance;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // 注意：不要在这里 delete 插件实例。
        // DLL_PROCESS_DETACH 阶段调用 join() 会因 loader lock 导致死锁崩溃。
        if (g_pluginInstance) {
            g_pluginInstance->Shutdown();
            g_pluginInstance = nullptr;
        }
        break;
    }
    return TRUE;
}

// ═══════════════════════════════════════════════
// CPowerItem 实现
// ═══════════════════════════════════════════════
const wchar_t* CPowerItem::GetItemName() const {
    return L"米家插座功率";
}

const wchar_t* CPowerItem::GetItemLableText() const {
    auto& cfg = ConfigManager::Instance().Get();
    if (cfg.showLabel)
        m_labelText = L"功率:";
    else
        m_labelText = L"";
    return m_labelText.c_str();
}

const wchar_t* CPowerItem::GetItemValueText() const {
    if (!m_plugin) { m_valueText = L"--"; return m_valueText.c_str(); }

    if (!m_plugin->IsConnected()) {
        auto& cfg = ConfigManager::Instance().Get();
        if (cfg.deviceIp.empty())
            m_valueText = L"未配置";
        else
            m_valueText = L"连接中...";
        return m_valueText.c_str();
    }

    double watts = m_plugin->GetCurrentWatts();
    auto& cfg = ConfigManager::Instance().Get();

    std::wostringstream oss;
    oss << std::fixed << std::setprecision(cfg.decimalPlaces) << watts;
    if (cfg.showUnit) oss << L"W";
    m_valueText = oss.str();
    return m_valueText.c_str();
}

// 任务栏内联功率迷你图：将当前功率归一化到 0..1（按 0~3000W 量程）
float CPowerItem::GetResourceUsageGraphValue() const {
    if (!m_plugin) return 0.0f;
    double w = m_plugin->GetCurrentWatts();
    float v = (float)(w / 3000.0);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

int CPowerItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) {
    if (type == MT_DBCLICKED && m_plugin) {
        m_plugin->OpenDashboard((HWND)hWnd);
        return 1;
    }
    return 0;
}

// ═══════════════════════════════════════════════
// CEnergyItem 实现（今日用电量，本地按功率积分估算）
// ═══════════════════════════════════════════════
const wchar_t* CEnergyItem::GetItemName() const {
    return L"今日用电";
}

const wchar_t* CEnergyItem::GetItemLableText() const {
    auto& cfg = ConfigManager::Instance().Get();
    m_labelText = cfg.showLabel ? L"用电:" : L"";
    return m_labelText.c_str();
}

const wchar_t* CEnergyItem::GetItemValueText() const {
    if (!m_plugin || !m_plugin->IsConnected()) {
        m_valueText = L"--";
        return m_valueText.c_str();
    }
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(2) << m_plugin->GetTodayKwh() << L"度";
    m_valueText = oss.str();
    return m_valueText.c_str();
}

// ═══════════════════════════════════════════════
// CMijiaPowerPlugin 实现
// ═══════════════════════════════════════════════
CMijiaPowerPlugin::CMijiaPowerPlugin() {
    m_powerItem.SetPlugin(this);
    m_energyItem.SetPlugin(this);
}

CMijiaPowerPlugin::~CMijiaPowerPlugin() {
    // 析构在普通线程上调用（不在 loader lock），join 是安全的
    m_stopFlag = true;
    if (m_sampleThread.joinable())
        m_sampleThread.join();

    // 保存今日用电量与历史记录
    auto& cfg = ConfigManager::Instance().Get();
    cfg.energyTodayWh = m_todayWh.load();
    cfg.energyDate = m_energyDate;
    ConfigManager::Instance().Save();
    if (cfg.enableRecording) {
        m_history.SaveToFile(ConfigManager::Instance().GetHistoryFilePath());
    }
}

// API v7：主程序在加载插件后调用此函数，传入 ITrafficMonitor*
void CMijiaPowerPlugin::OnInitialize(ITrafficMonitor* pApp) {
    m_pTM = pApp;
    // 注意：此时配置目录可能还未通过 OnExtenedInfo 传入。
    // 不在这里加载配置，等 OnExtenedInfo(EI_CONFIG_DIR) 时再初始化。
    // 但为了兼容老版本（未调用 OnInitialize），我们在 StartSampling 里做懒加载。
}

// 主程序通过此函数传入扩展信息，包括配置目录（EI_CONFIG_DIR）
void CMijiaPowerPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) {
    if (index == EI_CONFIG_DIR && data && data[0] != L'\0') {
        if (!m_initialized) {
            m_initialized = true;
            ConfigManager::Instance().SetConfigDir(data);
            ConfigManager::Instance().Load();
            MiioDevice::SetDebugLogPath(ConfigManager::Instance().GetConfigDir() + L"\\MijiaPower_debug.log");

            auto& cfg = ConfigManager::Instance().Get();
            if (cfg.enableRecording) {
                m_history.LoadFromFile(ConfigManager::Instance().GetHistoryFilePath());
            }

            InitEnergy();   // 从配置恢复今日用电（跨天则清零）

            // 启动采样线程
            StartSampling();
        }
    }
}

void CMijiaPowerPlugin::StartSampling() {
    if (!m_sampleThread.joinable()) {
        m_sampleThread = std::thread(&CMijiaPowerPlugin::SampleLoop, this);
    }
}

void CMijiaPowerPlugin::SampleLoop() {
    // 首次连接
    ConnectDevice();

    int elapsed = 0;
    while (!m_stopFlag) {
        Sleep(1000);
        elapsed++;

        auto& cfg = ConfigManager::Instance().Get();
        int interval = cfg.updateIntervalSec;
        if (interval < 1) interval = 1;

        if (elapsed >= interval) {
            elapsed = 0;

            // 尝试读取功率
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            if (!m_device) {
                // 尝试重连
                if (!cfg.deviceIp.empty() && !cfg.deviceToken.empty()) {
                    char ip[256], token[256];
                    WideCharToMultiByte(CP_ACP, 0, cfg.deviceIp.c_str(),    -1, ip,    256, NULL, NULL);
                    WideCharToMultiByte(CP_ACP, 0, cfg.deviceToken.c_str(), -1, token, 256, NULL, NULL);
                    try {
                        auto dev = std::make_unique<MiioDevice>(ip, token, 5000);
                        double w = 0;
                        if (dev->GetPower(w)) {
                            m_device   = std::move(dev);
                            m_connected = true;
                            m_currentWatts = w;
                            if (cfg.enableRecording)
                                m_history.AddSample(w);
                        }
                    } catch (...) {}
                }
            } else {
                // 已连接，读取数据
                double w = 0;
                if (m_device->GetPower(w)) {
                    m_connected    = true;
                    m_currentWatts = w;
                    if (cfg.enableRecording)
                        m_history.AddSample(w);
                    UpdateEnergy(w, interval);   // 今日用电量按功率积分
                } else {
                    // 连接失效，重置
                    m_device.reset();
                    m_connected = false;
                }
            }
        }
    }

    // 线程退出前保存历史
    auto& cfg = ConfigManager::Instance().Get();
    if (cfg.enableRecording) {
        m_history.SaveToFile(ConfigManager::Instance().GetHistoryFilePath());
    }
}

void CMijiaPowerPlugin::ConnectDevice() {
    auto& cfg = ConfigManager::Instance().Get();
    if (cfg.deviceIp.empty() || cfg.deviceToken.empty()) return;

    char ip[256], token[256];
    WideCharToMultiByte(CP_ACP, 0, cfg.deviceIp.c_str(),    -1, ip,    256, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, cfg.deviceToken.c_str(), -1, token, 256, NULL, NULL);

    try {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        auto dev = std::make_unique<MiioDevice>(ip, token, 5000);
        double w = 0;
        if (dev->GetPower(w)) {
            m_device   = std::move(dev);
            m_connected = true;
            m_currentWatts = w;
        }
    } catch (...) {}
}

void CMijiaPowerPlugin::DisconnectDevice() {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    m_device.reset();
    m_connected = false;
}

double CMijiaPowerPlugin::GetCurrentWatts() const {
    return m_currentWatts.load();
}

std::wstring CMijiaPowerPlugin::TodayStr() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    wchar_t buf[16];
    wcsftime(buf, 16, L"%Y%m%d", &lt);
    return buf;
}

void CMijiaPowerPlugin::InitEnergy() {
    auto& cfg = ConfigManager::Instance().Get();
    std::wstring today = TodayStr();
    if (cfg.energyDate == today) {
        m_todayWh = cfg.energyTodayWh;
        m_energyDate = cfg.energyDate;
    } else {
        m_todayWh = 0.0;
        m_energyDate = today;
        cfg.energyDate = today;
        cfg.energyTodayWh = 0.0;
    }
}

void CMijiaPowerPlugin::UpdateEnergy(double watts, int intervalSec) {
    std::wstring today = TodayStr();
    if (today != m_energyDate) {     // 跨天清零
        m_energyDate = today;
        m_todayWh = 0.0;
    }
    double delta = watts * intervalSec / 3600.0;   // Wh = W * s / 3600
    // C++17 下 std::atomic<double> 无 fetch_add，用 compare_exchange 累加
    double cur = m_todayWh.load();
    while (!m_todayWh.compare_exchange_weak(cur, cur + delta)) {}
    if (++m_persistCnt >= 20) {      // 约每分钟落盘一次
        m_persistCnt = 0;
        auto& cfg = ConfigManager::Instance().Get();
        cfg.energyTodayWh = m_todayWh.load();
        cfg.energyDate = m_energyDate;
        ConfigManager::Instance().Save();
    }
}

// ─── ITMPlugin 接口 ───
IPluginItem* CMijiaPowerPlugin::GetItem(int index) {
    if (index == 0) return &m_powerItem;
    if (index == 1) return &m_energyItem;
    return nullptr;
}

void CMijiaPowerPlugin::DataRequired() {
    // TrafficMonitor 定期调用此函数刷新数据
    // 数据由后台线程持续更新，无需在此处理
    // 但如果配置目录还未通过 OnExtenedInfo 传来，尝试用当前目录初始化（兼容性）
    if (!m_initialized) {
        m_initialized = true;
        wchar_t buf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, buf);
        ConfigManager::Instance().SetConfigDir(buf);
        ConfigManager::Instance().Load();
        MiioDevice::SetDebugLogPath(ConfigManager::Instance().GetConfigDir() + L"\\MijiaPower_debug.log");
        auto& cfg = ConfigManager::Instance().Get();
        if (cfg.enableRecording) {
            m_history.LoadFromFile(ConfigManager::Instance().GetHistoryFilePath());
        }
        InitEnergy();
        StartSampling();
    }
}

const wchar_t* CMijiaPowerPlugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
    case TMI_NAME:        return L"米家空调伴侣";
    case TMI_DESCRIPTION: return L"实时功率/用电，支持功率图与电量图，并可电脑控制空调（开关/温度/模式/风速/摆风）";
    case TMI_AUTHOR:      return L"MijiaPlug";
    case TMI_COPYRIGHT:   return L"2024 MijiaPlug";
    case TMI_URL:         return L"";
    case TMI_VERSION:     return L"1.1.1";
    default:              return L"";
    }
}

// 返回值改为 OptionReturn
ITMPlugin::OptionReturn CMijiaPowerPlugin::ShowOptionsDialog(void* hParent) {
    bool changed = COptionsDlg::Show((HWND)hParent);
    if (changed) {
        // 配置已更新，重新连接设备
        DisconnectDevice();
        // 重新加载历史（如果刚启用记录）
        auto& cfg = ConfigManager::Instance().Get();
        if (cfg.enableRecording && !m_history.HasData()) {
            m_history.LoadFromFile(ConfigManager::Instance().GetHistoryFilePath());
        }
        return OR_OPTION_CHANGED;
    }
    return OR_OPTION_UNCHANGED;
}

// ─── 空调控制辅助 ───
namespace {
    // ASCII std::string -> std::wstring（model 等均为 ASCII）
    std::wstring S2WS(const std::string& s) {
        std::wstring w; w.reserve(s.size());
        for (char c : s) w.push_back((wchar_t)(unsigned char)c);
        return w;
    }
    // 解析属性值（可能是数字、浮点、布尔或带引号的字符串）为整数
    bool ToIntVal(const std::string& v, int& out) {
        if (v.empty()) return false;
        std::string t = v;
        // 去掉首尾引号与空白
        size_t a = 0, b = t.size();
        while (a < b && (t[a] == '"' || t[a] == ' ' || t[a] == '\t')) a++;
        while (b > a && (t[b-1] == '"' || t[b-1] == ' ' || t[b-1] == '\t')) b--;
        if (a >= b) return false;
        std::string s = t.substr(a, b - a);
        if (s == "true")  { out = 1; return true; }   // bool 属性（如摆风）
        if (s == "false") { out = 0; return true; }
        try { out = (int)std::stod(s); return true; }
        catch (...) { return false; }
    }
}

bool CMijiaPowerPlugin::AcEnsureConnectedLocked() {
    if (m_device) { m_connected = true; return true; }
    auto& cfg = ConfigManager::Instance().Get();
    if (cfg.deviceIp.empty() || cfg.deviceToken.empty()) return false;
    char ip[256], token[256];
    WideCharToMultiByte(CP_ACP, 0, cfg.deviceIp.c_str(),    -1, ip,    256, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, cfg.deviceToken.c_str(), -1, token, 256, NULL, NULL);
    try {
        auto dev = std::make_unique<MiioDevice>(ip, token, 5000);
        double w = 0;
        if (dev->GetPower(w)) {
            m_device = std::move(dev);
            m_connected = true;
            m_currentWatts = w;
            return true;
        }
    } catch (...) {}
    return false;
}

bool CMijiaPowerPlugin::AcEnsureConnected() {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    return AcEnsureConnectedLocked();
}

bool CMijiaPowerPlugin::DeviceGetProperties(const std::vector<MiioProperty>& props, std::string& outResult) {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (!m_device) return false;
    return m_device->GetProperties(props, outResult);
}

bool CMijiaPowerPlugin::DeviceSetProperties(const std::vector<MiioPropValue>& vals, std::string& outResult) {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (!m_device) return false;
    return m_device->SetProperties(vals, outResult);
}

int CMijiaPowerPlugin::ParseFirstCode(const std::string& result) {
    // 在 result 数组内找第一个 "code":
    auto p = result.find("\"result\"");
    std::string arr = (p == std::string::npos) ? result : result.substr(p + 8);
    auto c = arr.find("\"code\"");
    if (c == std::string::npos) return -1;
    try { return std::stoi(arr.substr(c + 6)); } catch (...) { return -1; }
}

bool CMijiaPowerPlugin::AcSetPower(bool on, AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (!m_device && !AcEnsureConnectedLocked()) { out.lastError = L"设备未连接"; return false; }
    if (!m_device->SetPower(on)) { out.lastError = L"开关机指令发送失败"; return false; }
    out.power = on ? 1 : 0;
    return true;
}

bool CMijiaPowerPlugin::AcGetPowerState(int& out) {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (!m_device && !AcEnsureConnectedLocked()) return false;
    std::string s;
    if (!m_device->GetPowerState(s)) return false;
    out = (s == "on") ? 1 : 0;
    return true;
}

bool CMijiaPowerPlugin::AcRefreshState(AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    auto& cfg = ConfigManager::Instance().Get();
    if (!cfg.enableAcControl) { out.lastError = L"空调控制未启用"; return false; }
    if (!AcEnsureConnected()) { out.lastError = L"读取失败：设备未连接"; return false; }

    std::vector<MiioProperty> props = {
        { cfg.acModeSiid,  cfg.acModePiid },
        { cfg.acTempSiid,  cfg.acTempPiid },
        { cfg.acFanSiid,   cfg.acFanPiid },
        { cfg.acSwingSiid, cfg.acSwingPiid },
    };
    std::string result;
    if (!DeviceGetProperties(props, result)) {
        out.lastError = L"读取失败：通信错误"; return false;
    }
    auto parsed = MiioDevice::ParsePropResults(result);
    if (parsed.empty()) {
        out.lastCode = -1;
        out.lastError = L"设备未返回属性（siid/piid 可能不正确，或该设备不支持状态读取）";
        return false;
    }
    for (auto& r : parsed) {
        if (r.code != 0) { out.lastCode = r.code; out.lastError = L"设备返回错误码 " + std::to_wstring(r.code); }
        if (r.siid == cfg.acModeSiid && r.piid == cfg.acModePiid)        ToIntVal(r.value, out.mode);
        else if (r.siid == cfg.acTempSiid && r.piid == cfg.acTempPiid)   ToIntVal(r.value, out.temp);
        else if (r.siid == cfg.acFanSiid  && r.piid == cfg.acFanPiid)    ToIntVal(r.value, out.fan);
        else if (r.siid == cfg.acSwingSiid && r.piid == cfg.acSwingPiid) ToIntVal(r.value, out.swing);
    }
    return true;
}

bool CMijiaPowerPlugin::AcSetTemp(int t, AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    auto& cfg = ConfigManager::Instance().Get();
    if (!AcEnsureConnected()) { out.lastError = L"设置失败：设备未连接"; return false; }
    std::vector<MiioPropValue> vals = { { cfg.acTempSiid, cfg.acTempPiid, std::to_string(t) } };
    std::string result;
    if (!DeviceSetProperties(vals, result)) { out.lastError = L"设置失败：通信错误"; return false; }
    int code = ParseFirstCode(result); out.lastCode = code;
    if (code != 0) { out.lastError = L"设备返回错误码 " + std::to_wstring(code); return false; }
    out.temp = t; return true;
}

bool CMijiaPowerPlugin::AcSetMode(int m, AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    auto& cfg = ConfigManager::Instance().Get();
    if (!AcEnsureConnected()) { out.lastError = L"设置失败：设备未连接"; return false; }
    std::vector<MiioPropValue> vals = { { cfg.acModeSiid, cfg.acModePiid, std::to_string(m) } };
    std::string result;
    if (!DeviceSetProperties(vals, result)) { out.lastError = L"设置失败：通信错误"; return false; }
    int code = ParseFirstCode(result); out.lastCode = code;
    if (code != 0) { out.lastError = L"设备返回错误码 " + std::to_wstring(code); return false; }
    out.mode = m; return true;
}

bool CMijiaPowerPlugin::AcSetFan(int f, AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    auto& cfg = ConfigManager::Instance().Get();
    if (!AcEnsureConnected()) { out.lastError = L"设置失败：设备未连接"; return false; }
    std::vector<MiioPropValue> vals = { { cfg.acFanSiid, cfg.acFanPiid, std::to_string(f) } };
    std::string result;
    if (!DeviceSetProperties(vals, result)) { out.lastError = L"设置失败：通信错误"; return false; }
    int code = ParseFirstCode(result); out.lastCode = code;
    if (code != 0) { out.lastError = L"设备返回错误码 " + std::to_wstring(code); return false; }
    out.fan = f; return true;
}

bool CMijiaPowerPlugin::AcSetSwing(int s, AcState& out) {
    out.lastError.clear(); out.lastCode = 0;
    auto& cfg = ConfigManager::Instance().Get();
    if (!AcEnsureConnected()) { out.lastError = L"设置失败：设备未连接"; return false; }
    // 摆风为 bool 属性，value 需用 true/false
    std::vector<MiioPropValue> vals = { { cfg.acSwingSiid, cfg.acSwingPiid, (s ? "true" : "false") } };
    std::string result;
    if (!DeviceSetProperties(vals, result)) { out.lastError = L"设置失败：通信错误"; return false; }
    int code = ParseFirstCode(result); out.lastCode = code;
    if (code != 0) { out.lastError = L"设备返回错误码 " + std::to_wstring(code); return false; }
    out.swing = s; return true;
}

bool CMijiaPowerPlugin::AcDetectModel(std::wstring& out) {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (!m_device && !AcEnsureConnectedLocked()) { out = L""; return false; }
    std::string m;
    if (!m_device->GetDeviceInfo(m)) { out = L""; return false; }
    out = S2WS(m);
    auto& cfg = ConfigManager::Instance().Get();
    cfg.acModel = out;
    ConfigManager::Instance().Save();
    return true;
}

void CMijiaPowerPlugin::OpenDashboard(HWND hParent) {
    CDashboardDlg::Show(hParent, this);
}

// ─── 插件命令（右键菜单）───
int CMijiaPowerPlugin::GetCommandCount() { return 2; }

const wchar_t* CMijiaPowerPlugin::GetCommandName(int command_index) {
    if (command_index == 0) return L"查看图表 / 电量";
    if (command_index == 1) return L"空调控制面板";
    return nullptr;
}

void CMijiaPowerPlugin::OnPluginCommand(int command_index, void* hWnd, void* para) {
    (void)command_index; (void)para;
    OpenDashboard((HWND)hWnd);
}

const wchar_t* CMijiaPowerPlugin::GetTooltipInfo() {
    auto& cfg = ConfigManager::Instance().Get();

    std::wostringstream oss;
    oss << L"【" << cfg.deviceName << L"】";
    if (!m_connected) {
        oss << L"\n状态：未连接";
        if (!cfg.deviceIp.empty())
            oss << L"\nIP：" << cfg.deviceIp;
    } else {
        double w = m_currentWatts.load();
        oss << std::fixed << std::setprecision(1);
        oss << L"\n当前功率：" << w << L" W";
        oss << L"\n今日用电：" << std::setprecision(2) << (m_todayWh.load() / 1000.0) << L" 度";

        if (cfg.enableRecording) {
            auto st10m = m_history.GetStats(600);
            if (st10m.valid) {
                oss << L"\n--- 最近10分钟 ---"
                    << L"\n  最大：" << st10m.maxW << L" W"
                    << L"\n  最小：" << st10m.minW << L" W"
                    << L"\n  平均：" << st10m.avgW << L" W";
            }
            auto st1h = m_history.GetLongStats(1);
            if (st1h.valid) {
                oss << L"\n--- 最近1小时 ---"
                    << L"\n  最大：" << st1h.maxW << L" W"
                    << L"\n  最小：" << st1h.minW << L" W"
                    << L"\n  平均：" << st1h.avgW << L" W";
            }
            auto st24h = m_history.GetLongStats(24);
            if (st24h.valid) {
                oss << L"\n--- 最近24小时 ---"
                    << L"\n  最大：" << st24h.maxW << L" W"
                    << L"\n  最小：" << st24h.minW << L" W"
                    << L"\n  平均：" << st24h.avgW << L" W";
            }
        }
    }

    m_tooltipText = oss.str();
    return m_tooltipText.c_str();
}
