// PluginConfig.h - 插件配置管理（INI文件读写）
#pragma once
#include "pch.h"

struct PluginConfig {
    // 设备信息
    std::wstring deviceIp;
    std::wstring deviceToken;
    std::wstring deviceName    = L"米家插座";

    // 功能开关
    bool enableRecording   = true;    // 是否记录功率历史
    bool showLabel         = true;    // 是否显示标签"功率:"
    int  updateIntervalSec = 3;       // 采集间隔（秒）

    // 显示格式
    bool showUnit          = true;    // 是否显示 W 单位
    int  decimalPlaces     = 1;       // 小数位数（0/1/2）

    // 今日用电量（本地按功率积分估算）
    double       energyTodayWh = 0.0; // 今日累计 Wh
    std::wstring energyDate;          // 今日日期 YYYYMMDD，跨天清零用

    // ── 空调控制（米家空调伴侣）──
    bool        enableAcControl = false;   // 是否启用空调控制面板
    std::wstring acModel;                  // 设备 model（探测后写入）

    // MIoT SPEC 属性映射（siid/piid），默认按 lumi.acpartner.mcn02 / 通用空调服务
    // 不同固件可能不同，可在选项或 INI [ACMap] 中调整
    int acModeSiid  = 2, acModePiid  = 2;  // 模式
    int acTempSiid  = 2, acTempPiid  = 3;  // 目标温度
    int acFanSiid   = 2, acFanPiid   = 4;  // 风速
    int acSwingSiid = 2, acSwingPiid = 5;  // 摆风（垂直）
};

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager inst;
        return inst;
    }

    void  SetConfigDir(const std::wstring& dir) { m_dir = dir; }
    void  Load();
    void  Save() const;

    PluginConfig& Get() { return m_cfg; }
    const PluginConfig& Get() const { return m_cfg; }

    // 历史文件路径
    std::wstring GetHistoryFilePath() const;

private:
    std::wstring  m_dir;
    PluginConfig  m_cfg;
    std::wstring IniPath() const;

    static std::wstring ReadIniString(const std::wstring& section, const std::wstring& key,
                                      const std::wstring& def, const std::wstring& path);
    static int         ReadIniInt   (const std::wstring& section, const std::wstring& key,
                                      int def, const std::wstring& path);
    static bool        ReadIniBool  (const std::wstring& section, const std::wstring& key,
                                      bool def, const std::wstring& path);
    static double      ReadIniDouble(const std::wstring& section, const std::wstring& key,
                                      double def, const std::wstring& path);
};
