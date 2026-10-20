// sysmon.h — lightweight CPU load and memory usage sampling (Mach/sysctl).
#pragma once

#include <cstdint>

struct SysUsage {
    double cpu = 0;        // CPU busy, percent 0..100
    double memPercent = 0; // memory used, percent 0..100
    double memUsedGB = 0;
    double memTotalGB = 0;
};

class SysMonitor {
public:
    SysUsage sample();

private:
    bool havePrev_ = false;
    uint64_t prevUser_ = 0, prevSys_ = 0, prevIdle_ = 0, prevNice_ = 0;
    SysUsage last_;
};
