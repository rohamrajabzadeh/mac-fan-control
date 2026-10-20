#include "smc.h"

#include <unistd.h>
#include <cstring>
#include <cmath>

namespace smc {

// ---- SMC protocol constants / structures (canonical AppleSMC layout) ----
static const int KERNEL_INDEX_SMC = 2;

static const uint8_t SMC_CMD_READ_BYTES   = 5;
static const uint8_t SMC_CMD_WRITE_BYTES  = 6;
static const uint8_t SMC_CMD_READ_INDEX   = 8;
static const uint8_t SMC_CMD_READ_KEYINFO = 9;

// Natural alignment (NOT packed): the canonical AppleSMC struct is 80 bytes
// with default alignment, matching the 0x50 size the kernel method expects.
typedef struct {
    uint8_t  major;
    uint8_t  minor;
    uint8_t  build;
    uint8_t  reserved;
    uint16_t release;
} SMCVersion;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpuPLimit;
    uint32_t gpuPLimit;
    uint32_t memPLimit;
} SMCPLimitData;

typedef struct {
    uint32_t dataSize;
    uint32_t dataType;
    uint8_t  dataAttributes;
} SMCKeyInfoData;

typedef struct {
    uint32_t        key;
    SMCVersion      vers;
    SMCPLimitData   pLimitData;
    SMCKeyInfoData  keyInfo;
    uint8_t         result;
    uint8_t         status;
    uint8_t         data8;
    uint32_t        data32;
    uint8_t         bytes[32];
} SMCKeyData; // 80 bytes == 0x50

static_assert(sizeof(SMCKeyData) == 80, "SMCKeyData must be 80 bytes (0x50)");

// ---- FourCC helpers ----
uint32_t keyToUInt32(const std::string &key) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v = v << 8;
        if (i < (int)key.size()) v |= (uint8_t)key[i];
    }
    return v;
}

std::string uint32ToKey(uint32_t v) {
    char k[5] = {0};
    k[0] = (char)((v >> 24) & 0xff);
    k[1] = (char)((v >> 16) & 0xff);
    k[2] = (char)((v >> 8) & 0xff);
    k[3] = (char)(v & 0xff);
    return std::string(k, 4);
}

// ---- Value decoding ----
static std::string trim(const std::string &s) {
    std::string r = s;
    while (!r.empty() && (r.back() == ' ' || r.back() == '\0')) r.pop_back();
    return r;
}

double Value::toDouble() const {
    if (!valid || dataSize == 0) return 0.0;
    std::string t = trim(type);

    // IEEE float, little-endian (common on Apple Silicon for temps & fans).
    if (t == "flt" && dataSize == 4) {
        float f;
        std::memcpy(&f, bytes, 4);
        return (double)f;
    }
    // Signed fixed point 7.8 (classic Intel temperature format).
    if (t == "sp78" && dataSize >= 2) {
        int16_t raw = (int16_t)((bytes[0] << 8) | bytes[1]);
        return raw / 256.0;
    }
    // Unsigned fixed point, exponent 2 (classic fan RPM format fpe2).
    if (t == "fpe2" && dataSize >= 2) {
        uint16_t raw = (uint16_t)((bytes[0] << 8) | bytes[1]);
        return raw / 4.0;
    }
    // Unsigned fixed point 8.8.
    if (t == "fp88" && dataSize >= 2) {
        uint16_t raw = (uint16_t)((bytes[0] << 8) | bytes[1]);
        return raw / 256.0;
    }
    if ((t == "ui8" || t == "si8") && dataSize >= 1) {
        return t == "si8" ? (double)(int8_t)bytes[0] : (double)bytes[0];
    }
    if ((t == "ui16" || t == "si16") && dataSize >= 2) {
        uint16_t raw = (uint16_t)((bytes[0] << 8) | bytes[1]);
        return t == "si16" ? (double)(int16_t)raw : (double)raw;
    }
    if ((t == "ui32" || t == "si32") && dataSize >= 4) {
        uint32_t raw = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                       ((uint32_t)bytes[2] << 8) | bytes[3];
        return t == "si32" ? (double)(int32_t)raw : (double)raw;
    }
    return 0.0;
}

bool encodeForType(const std::string &typeRaw, uint32_t size, double value,
                   uint8_t *out, uint32_t &outSize) {
    std::string t = trim(typeRaw);
    std::memset(out, 0, 32);

    if (t == "flt" && size == 4) {
        float f = (float)value;
        std::memcpy(out, &f, 4);
        outSize = 4;
        return true;
    }
    if (t == "fpe2" && size >= 2) {
        if (value < 0) value = 0;
        uint16_t raw = (uint16_t)std::lround(value * 4.0);
        out[0] = (uint8_t)(raw >> 8);
        out[1] = (uint8_t)(raw & 0xff);
        outSize = 2;
        return true;
    }
    if (t == "ui8" && size >= 1) {
        out[0] = (uint8_t)std::lround(value);
        outSize = 1;
        return true;
    }
    if (t == "ui16" && size >= 2) {
        uint16_t raw = (uint16_t)std::lround(value);
        out[0] = (uint8_t)(raw >> 8);
        out[1] = (uint8_t)(raw & 0xff);
        outSize = 2;
        return true;
    }
    return false;
}

// ---- Connection ----
Connection::~Connection() { close(); }

bool Connection::haveRoot() { return geteuid() == 0; }

bool Connection::open() {
    if (conn_) return true;
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!service) return false;
    IOReturn r = IOServiceOpen(service, mach_task_self(), 0, &conn_);
    IOObjectRelease(service);
    lastResult_ = r;
    if (r != kIOReturnSuccess) {
        conn_ = 0;
        return false;
    }
    return true;
}

void Connection::close() {
    if (conn_) {
        IOServiceClose(conn_);
        conn_ = 0;
    }
}

IOReturn Connection::call(void *inputStruct, void *outputStruct) {
    size_t outSize = sizeof(SMCKeyData);
    return IOConnectCallStructMethod(conn_, KERNEL_INDEX_SMC, inputStruct,
                                     sizeof(SMCKeyData), outputStruct,
                                     &outSize);
}

bool Connection::read(const std::string &key, Value &out) {
    out = Value{};
    if (!conn_) return false;

    SMCKeyData in{}, res{};
    in.key = keyToUInt32(key);
    in.data8 = SMC_CMD_READ_KEYINFO;
    lastResult_ = call(&in, &res);
    if (lastResult_ != kIOReturnSuccess) return false;

    uint32_t dataSize = res.keyInfo.dataSize;
    uint32_t dataType = res.keyInfo.dataType;
    if (dataSize == 0 || dataSize > 32) return false;

    SMCKeyData in2{}, res2{};
    in2.key = keyToUInt32(key);
    in2.keyInfo.dataSize = dataSize;
    in2.data8 = SMC_CMD_READ_BYTES;
    lastResult_ = call(&in2, &res2);
    if (lastResult_ != kIOReturnSuccess) return false;

    out.key = key;
    out.type = uint32ToKey(dataType);
    out.dataSize = dataSize;
    std::memcpy(out.bytes, res2.bytes, dataSize);
    out.valid = true;
    return true;
}

bool Connection::write(const std::string &key, const uint8_t *data,
                       uint32_t size) {
    if (!conn_ || size == 0 || size > 32) return false;

    // Look up the key's declared size to validate.
    SMCKeyData in{}, res{};
    in.key = keyToUInt32(key);
    in.data8 = SMC_CMD_READ_KEYINFO;
    lastResult_ = call(&in, &res);
    if (lastResult_ != kIOReturnSuccess) return false;
    if (res.keyInfo.dataSize != size) return false;

    SMCKeyData win{}, wres{};
    win.key = keyToUInt32(key);
    win.data8 = SMC_CMD_WRITE_BYTES;
    win.keyInfo.dataSize = size;
    std::memcpy(win.bytes, data, size);
    lastResult_ = call(&win, &wres);
    return lastResult_ == kIOReturnSuccess;
}

uint32_t Connection::keyCount() {
    Value v;
    if (!read("#KEY", v)) return 0;
    double d = v.toDouble();
    if (d <= 0) {
        // #KEY is often ui32 big-endian already handled by toDouble; fall back.
        uint32_t raw = ((uint32_t)v.bytes[0] << 24) | ((uint32_t)v.bytes[1] << 16) |
                       ((uint32_t)v.bytes[2] << 8) | v.bytes[3];
        return raw;
    }
    return (uint32_t)d;
}

std::string Connection::keyAtIndex(uint32_t index) {
    if (!conn_) return {};
    SMCKeyData in{}, res{};
    in.data8 = SMC_CMD_READ_INDEX;
    in.data32 = index;
    lastResult_ = call(&in, &res);
    if (lastResult_ != kIOReturnSuccess) return {};
    return uint32ToKey(res.key);
}

} // namespace smc
