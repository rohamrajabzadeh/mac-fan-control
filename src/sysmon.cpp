#include "sysmon.h"

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>

SysUsage SysMonitor::sample() {
    SysUsage u = last_;

    // --- CPU load (delta of busy vs total ticks since last sample) ---
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&info, &count) == KERN_SUCCESS) {
        uint64_t user = info.cpu_ticks[CPU_STATE_USER];
        uint64_t sys = info.cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t idle = info.cpu_ticks[CPU_STATE_IDLE];
        uint64_t nice = info.cpu_ticks[CPU_STATE_NICE];
        if (havePrev_) {
            uint64_t du = user - prevUser_, ds = sys - prevSys_;
            uint64_t di = idle - prevIdle_, dn = nice - prevNice_;
            uint64_t total = du + ds + di + dn;
            uint64_t busy = du + ds + dn;
            if (total > 0) u.cpu = (double)busy * 100.0 / (double)total;
        }
        prevUser_ = user; prevSys_ = sys; prevIdle_ = idle; prevNice_ = nice;
        havePrev_ = true;
    }

    // --- Memory used (active + wired + compressed) / total ---
    int64_t total = 0;
    size_t len = sizeof(total);
    sysctlbyname("hw.memsize", &total, &len, nullptr, 0);

    vm_size_t page = 0;
    host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vm;
    mach_msg_type_number_t vmCount = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &vmCount) == KERN_SUCCESS &&
        total > 0) {
        uint64_t used = ((uint64_t)vm.active_count + vm.wire_count +
                         vm.compressor_page_count) * (uint64_t)page;
        u.memTotalGB = (double)total / 1e9;
        u.memUsedGB = (double)used / 1e9;
        u.memPercent = (double)used * 100.0 / (double)total;
    }

    last_ = u;
    return u;
}
