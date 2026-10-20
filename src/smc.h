// smc.h — Low-level AppleSMC access via IOKit.
//
// Mechanism mirrors what the Macs Fan Control privileged helper does:
//   IOServiceMatching("AppleSMC") -> IOServiceOpen -> IOConnectCallStructMethod
// with an 80-byte SMCKeyData_t structure. The data8 command field selects
// read-info / read-bytes / write-bytes; the IOKit method index is always 2
// (KERNEL_INDEX_SMC). Reading needs no privilege; writing requires root.
//
// Personal-use project. Writing wrong SMC keys can be harmful — only fan
// keys (F*) are ever written by this app.
#pragma once

#include <IOKit/IOKitLib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace smc {

// 4-char FourCC key <-> UInt32 helpers.
uint32_t keyToUInt32(const std::string &key);
std::string uint32ToKey(uint32_t v);

// A decoded SMC value.
struct Value {
    std::string key;         // e.g. "TC0P"
    std::string type;        // e.g. "flt ", "sp78", "fpe2", "ui8 "
    uint32_t dataSize = 0;   // raw byte count
    uint8_t bytes[32] = {0}; // raw bytes
    bool valid = false;

    // Interpret the raw bytes as a real number according to `type`.
    double toDouble() const;
};

class Connection {
public:
    Connection() = default;
    ~Connection();

    bool open();
    void close();
    bool isOpen() const { return conn_ != 0; }

    // Returns euid == 0. Writes only succeed when true.
    static bool haveRoot();

    // Read a single key (fetches key info then bytes).
    bool read(const std::string &key, Value &out);

    // Write raw bytes to a key. The byte count must match the key's declared
    // size (looked up from the SMC first). Returns true on kIOReturnSuccess.
    bool write(const std::string &key, const uint8_t *data, uint32_t size);

    // Total number of keys reported by the SMC (#KEY).
    uint32_t keyCount();

    // Resolve the key name at a given enumeration index (SMC_CMD_READ_INDEX).
    std::string keyAtIndex(uint32_t index);

    IOReturn lastIOReturn() const { return lastResult_; }

private:
    // One raw IOConnectCallStructMethod round-trip.
    IOReturn call(void *inputStruct, void *outputStruct);

    io_connect_t conn_ = 0;
    IOReturn lastResult_ = 0;
};

// Encode a double into bytes matching an existing key type (for writing fan
// targets). Supports fpe2 / flt / ui8 / ui16. Returns false for unknown types.
bool encodeForType(const std::string &type, uint32_t size, double value,
                   uint8_t *out, uint32_t &outSize);

} // namespace smc
