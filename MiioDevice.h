// MiioDevice.h - 纯C++ miIO协议实现（AES-128-CBC + UDP）
// 参考 Python 原版 miio_proto.py
#pragma once
#include "pch.h"

// ─── MD5 简易实现 ───
namespace MiioMD5 {
    struct MD5Context {
        unsigned int state[4];
        unsigned int count[2];
        unsigned char buffer[64];
    };
    void Init(MD5Context* ctx);
    void Update(MD5Context* ctx, const unsigned char* data, unsigned int len);
    void Final(unsigned char digest[16], MD5Context* ctx);
    void Compute(const unsigned char* data, size_t len, unsigned char out[16]);
}

// ─── AES-128-CBC 简易实现 ───
namespace MiioAES {
    // 加密（PKCS7 Padding，输入明文，输出密文，长度必须是16的倍数+padding后）
    bool Encrypt(const unsigned char key[16], const unsigned char iv[16],
                 const unsigned char* plaintext, size_t plainLen,
                 std::vector<unsigned char>& ciphertext);
    bool Decrypt(const unsigned char key[16], const unsigned char iv[16],
                 const unsigned char* ciphertext, size_t cipherLen,
                 std::vector<unsigned char>& plaintext);
}

// ─── miIO 设备类 ───
struct MiioDeviceConfig {
    std::wstring ip;
    std::wstring token;
    std::wstring name;
    std::wstring model;
    bool autoConnect = false;
};

struct MiioProperty {
    int siid;
    int piid;
};

// SPEC set_properties 的单个写属性（valueJson 为已是合法 JSON 的值，如 "26" / "\"cool\"" / "true"）
struct MiioPropValue {
    int siid;
    int piid;
    std::string valueJson;
};

// get_properties 结果中单个属性的解析结果
struct MiioPropResult {
    int siid = 0;
    int piid = 0;
    int code = -1;       // miIO 返回码，0 表示成功
    std::string value;   // 原始 value 字符串（数字/字符串/对象等）
    bool valid = false;
};

class MiioDevice {
public:
    static const int PORT = 54321;

    explicit MiioDevice(const std::string& ip, const std::string& token, int timeoutMs = 5000);
    ~MiioDevice() = default;

    // 调试日志：记录 get/set_properties 的原始请求与响应（设为空串则关闭）
    static void SetDebugLogPath(const std::wstring& path) { s_debugLogPath = path; }

    bool Handshake();
    bool IsHandshaked() const { return m_handshaked; }

    // 发送命令，返回 result 字段 JSON 字符串
    bool Send(const std::string& method, const std::string& paramsJson, std::string& outResult);

    // 获取功率 (W)
    bool GetPower(double& outWatts);

    // ── 空调伴侣控制（miIO 旧协议 + MIoT SPEC）──
    // 旧协议：开关机（对空调伴侣最可靠，走红外转发）
    bool SetPower(bool on);
    bool GetPowerState(std::string& outState);   // get_prop ["power"] -> "on"/"off"
    bool GetDeviceInfo(std::string& outModel);   // get_device_info -> model 字段

    // lumi 空调伴侣旧协议：读取 AC 型号码+状态串；send_cmd 下发控制码
    // state 位编码: [2前缀][power][mode][fan][1-swing][temp(16进制2位)][led]...
    bool GetModelAndState(std::string& outModel, std::string& outState, int& outPower);
    bool SendCmd(const std::string& code);       // send_cmd ["code"] -> 成功返回 ["ok"]

    // lumi.acpartner.mcn02 协议：get_prop 命名属性读状态；set_xxx 下发
    bool GetAcStatus(std::vector<std::string>& outValues);  // get_prop 6 项 -> ["on","cool",28,"small_fan","on",441.0]
    bool SendAcSet(const std::string& method, const std::string& paramsJson);  // set_tar_temp/set_mode/... -> ["ok"]

    // MIoT SPEC：批量读/写属性
    bool GetProperties(const std::vector<MiioProperty>& props, std::string& outResult);
    bool SetProperties(const std::vector<MiioPropValue>& vals,  std::string& outResult);

    // 解析 get_properties / set_properties 的 result 数组，提取每个属性的 code 与 value
    static std::vector<MiioPropResult> ParsePropResults(const std::string& result);

private:
    std::string  m_ip;
    unsigned char m_token[16];
    int          m_timeoutMs;
    unsigned char m_key[16];
    unsigned char m_iv[16];
    unsigned int  m_deviceId  = 0;
    unsigned int  m_serverStamp = 0;
    long long     m_stampDelta  = 0;
    int           m_msgId      = 1;
    bool          m_handshaked = false;

    std::vector<unsigned char> Encrypt(const std::string& plaintext);
    std::string Decrypt(const std::vector<unsigned char>& ciphertext);
    std::vector<unsigned char> BuildPacket(const std::string& payloadJson);
    std::string ParsePacket(const std::vector<unsigned char>& data);
    bool UdpSendRecv(const std::vector<unsigned char>& sendBuf,
                     std::vector<unsigned char>& recvBuf, int recvMax = 4096);
    unsigned int CurrentStamp() const;

    static std::wstring s_debugLogPath;
    static void DebugLog(const std::string& s);
};
