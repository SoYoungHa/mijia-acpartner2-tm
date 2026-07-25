// PluginConfig.cpp - 配置管理实现（使用 Win32 INI API）
#include "pch.h"
#include "PluginConfig.h"

std::wstring ConfigManager::IniPath() const {
    return m_dir + L"\\MijiaPower.ini";
}

std::wstring ConfigManager::GetHistoryFilePath() const {
    return m_dir + L"\\MijiaPower_history.json";
}

std::wstring ConfigManager::ReadIniString(const std::wstring& section, const std::wstring& key,
                                           const std::wstring& def, const std::wstring& path) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), def.c_str(), buf, 512, path.c_str());
    return buf;
}

int ConfigManager::ReadIniInt(const std::wstring& section, const std::wstring& key,
                               int def, const std::wstring& path) {
    return (int)GetPrivateProfileIntW(section.c_str(), key.c_str(), def, path.c_str());
}

bool ConfigManager::ReadIniBool(const std::wstring& section, const std::wstring& key,
                                 bool def, const std::wstring& path) {
    return ReadIniInt(section, key, def ? 1 : 0, path) != 0;
}

double ConfigManager::ReadIniDouble(const std::wstring& section, const std::wstring& key,
                                     double def, const std::wstring& path) {
    std::wstring s = ReadIniString(section, key, L"", path);
    if (s.empty()) return def;
    return _wtof(s.c_str());
}

void ConfigManager::Load() {
    auto p = IniPath();
    m_cfg.deviceIp    = ReadIniString(L"Device", L"IP",    L"",      p);
    m_cfg.deviceToken = ReadIniString(L"Device", L"Token", L"",      p);
    m_cfg.deviceName  = ReadIniString(L"Device", L"Name",  L"米家插座", p);

    m_cfg.enableRecording   = ReadIniBool(L"Plugin", L"EnableRecording",   true, p);
    m_cfg.showLabel         = ReadIniBool(L"Plugin", L"ShowLabel",         true, p);
    m_cfg.showUnit          = ReadIniBool(L"Plugin", L"ShowUnit",          true, p);
    m_cfg.updateIntervalSec = ReadIniInt (L"Plugin", L"UpdateIntervalSec", 3,    p);
    m_cfg.decimalPlaces     = ReadIniInt (L"Plugin", L"DecimalPlaces",     1,    p);

    m_cfg.energyTodayWh = ReadIniDouble(L"Energy", L"TodayWh", 0.0, p);
    m_cfg.energyDate    = ReadIniString(L"Energy", L"Date",    L"",  p);

    // 空调控制
    m_cfg.enableAcControl = ReadIniBool (L"AC", L"EnableControl", false, p);
    m_cfg.acModel         = ReadIniString(L"AC", L"Model", L"", p);

    m_cfg.acModeSiid  = ReadIniInt(L"ACMap", L"ModeSiid",  2, p);
    m_cfg.acModePiid  = ReadIniInt(L"ACMap", L"ModePiid",  2, p);
    m_cfg.acTempSiid  = ReadIniInt(L"ACMap", L"TempSiid",  2, p);
    m_cfg.acTempPiid  = ReadIniInt(L"ACMap", L"TempPiid",  3, p);
    m_cfg.acFanSiid   = ReadIniInt(L"ACMap", L"FanSiid",   3, p);
    m_cfg.acFanPiid   = ReadIniInt(L"ACMap", L"FanPiid",   1, p);
    m_cfg.acSwingSiid = ReadIniInt(L"ACMap", L"SwingSiid", 3, p);
    m_cfg.acSwingPiid = ReadIniInt(L"ACMap", L"SwingPiid", 2, p);

    // 一次性迁移：旧版本误把 fan/swing 设为 siid=2（mcn02 上不存在），此处纠正
    m_cfg.acMapVer = ReadIniInt(L"ACMap", L"MapVer", 0, p);
    if (m_cfg.acMapVer < 2) {
        bool corrected = false;
        if (m_cfg.acFanSiid == 2 && m_cfg.acFanPiid == 4)    { m_cfg.acFanSiid = 3; m_cfg.acFanPiid = 1; corrected = true; }
        if (m_cfg.acSwingSiid == 2 && m_cfg.acSwingPiid == 5){ m_cfg.acSwingSiid = 3; m_cfg.acSwingPiid = 2; corrected = true; }
        m_cfg.acMapVer = 2;
        if (corrected) ConfigManager::Instance().Save();   // 立即落盘，避免重复迁移
    }

    // 约束
    if (m_cfg.updateIntervalSec < 1)  m_cfg.updateIntervalSec = 1;
    if (m_cfg.updateIntervalSec > 60) m_cfg.updateIntervalSec = 60;
    if (m_cfg.decimalPlaces < 0) m_cfg.decimalPlaces = 0;
    if (m_cfg.decimalPlaces > 2) m_cfg.decimalPlaces = 2;
}

void ConfigManager::Save() const {
    auto p = IniPath();
    WritePrivateProfileStringW(L"Device", L"IP",    m_cfg.deviceIp.c_str(),    p.c_str());
    WritePrivateProfileStringW(L"Device", L"Token", m_cfg.deviceToken.c_str(), p.c_str());
    WritePrivateProfileStringW(L"Device", L"Name",  m_cfg.deviceName.c_str(),  p.c_str());

    WritePrivateProfileStringW(L"Plugin", L"EnableRecording",   m_cfg.enableRecording   ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowLabel",         m_cfg.showLabel         ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Plugin", L"ShowUnit",          m_cfg.showUnit          ? L"1" : L"0", p.c_str());

    wchar_t buf[32];
    _itow_s(m_cfg.updateIntervalSec, buf, 10);
    WritePrivateProfileStringW(L"Plugin", L"UpdateIntervalSec", buf, p.c_str());
    _itow_s(m_cfg.decimalPlaces, buf, 10);
    WritePrivateProfileStringW(L"Plugin", L"DecimalPlaces", buf, p.c_str());

    swprintf_s(buf, L"%.4f", m_cfg.energyTodayWh);
    WritePrivateProfileStringW(L"Energy", L"TodayWh", buf, p.c_str());
    WritePrivateProfileStringW(L"Energy", L"Date",    m_cfg.energyDate.c_str(), p.c_str());

    // 空调控制
    WritePrivateProfileStringW(L"AC", L"EnableControl",
        m_cfg.enableAcControl ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"AC", L"Model", m_cfg.acModel.c_str(), p.c_str());

    auto wint = [](int v) { wchar_t b[32]; _itow_s(v, b, 32); return std::wstring(b); };
    WritePrivateProfileStringW(L"ACMap", L"ModeSiid",  wint(m_cfg.acModeSiid).c_str(),  p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"ModePiid",  wint(m_cfg.acModePiid).c_str(),  p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"TempSiid",  wint(m_cfg.acTempSiid).c_str(),  p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"TempPiid",  wint(m_cfg.acTempPiid).c_str(),  p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"FanSiid",   wint(m_cfg.acFanSiid).c_str(),   p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"FanPiid",   wint(m_cfg.acFanPiid).c_str(),   p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"SwingSiid", wint(m_cfg.acSwingSiid).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"SwingPiid", wint(m_cfg.acSwingPiid).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ACMap", L"MapVer",     wint(m_cfg.acMapVer).c_str(),    p.c_str());
}
