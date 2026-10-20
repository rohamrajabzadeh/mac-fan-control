// sysmon.h — lightweight CPU, memory, swap and disk sampling (Mach/sysctl).
#pragma once

#include <cstdint>

struct SysUsage {
    double cpu = 0;            // CPU busy, % (user + system + nice)
    double cpuUser = 0;        // %
    double cpuSystem = 0;      // %
    double memPercent = 0;     // app + wired + compressed, % of physical
    double memUsedGB = 0;      // GiB
    double memTotalGB = 0;     // GiB
    double swapUsedGB = 0;     // GiB
    double swapTotalGB = 0;    // GiB
    double swapPercent = 0;
    double diskUsedGB = 0;     // GB (decimal, like Finder)
    double diskTotalGB = 0;
    double diskPercent = 0;
};

class SysMonitor {
public:
    SysUsage sample();

private:
    bool havePrev_ = false;
    uint64_t prevUser_ = 0, prevSys_ = 0, prevIdle_ = 0, prevNice_ = 0;
    SysUsage last_;
};
