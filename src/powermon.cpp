#include "powermon.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <notify.h>
#include <algorithm>
#include <cstring>

// --- IOReport (libIOReport.dylib; private but stable, used by macmon & co) ---
typedef struct IOReportSubscription *IOReportSubscriptionRef;
extern "C" {
CFMutableDictionaryRef IOReportCopyChannelsInGroup(CFStringRef group, CFStringRef subgroup,
                                                   uint64_t a, uint64_t b, uint64_t c);
void IOReportMergeChannels(CFMutableDictionaryRef a, CFMutableDictionaryRef b, CFTypeRef unused);
IOReportSubscriptionRef IOReportCreateSubscription(void *a, CFMutableDictionaryRef desired,
                                                   CFMutableDictionaryRef *subbed,
                                                   uint64_t channelId, CFTypeRef b);
CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef sub, CFMutableDictionaryRef subbed,
                                      CFTypeRef a);
CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef prev, CFDictionaryRef cur, CFTypeRef a);
int64_t IOReportSimpleGetIntegerValue(CFDictionaryRef item, int32_t idx);
CFStringRef IOReportChannelGetGroup(CFDictionaryRef item);
CFStringRef IOReportChannelGetSubGroup(CFDictionaryRef item);
CFStringRef IOReportChannelGetChannelName(CFDictionaryRef item);
CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef item);
int32_t IOReportStateGetCount(CFDictionaryRef item);
CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef item, int32_t idx);
int64_t IOReportStateGetResidency(CFDictionaryRef item, int32_t idx);
}

static QString qs(CFStringRef s) {
    if (!s) return {};
    char buf[256];
    if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return QString::fromUtf8(buf);
    return {};
}

// Read a pmgr DVFS table ("voltage-states*"): pairs of (freq, voltage) uint32.
static QVector<double> dvfsTable(io_object_t pmgr, const char *key) {
    QVector<double> out;
    CFStringRef k = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    CFTypeRef prop = IORegistryEntryCreateCFProperty(pmgr, k, kCFAllocatorDefault, 0);
    CFRelease(k);
    if (!prop) return out;
    if (CFGetTypeID(prop) == CFDataGetTypeID()) {
        CFDataRef d = (CFDataRef)prop;
        const uint8_t *p = CFDataGetBytePtr(d);
        CFIndex n = CFDataGetLength(d) / 8;
        for (CFIndex i = 0; i < n; ++i) {
            uint32_t f;
            std::memcpy(&f, p + i * 8, 4);
            if (f == 0) continue;
            double mhz = f > 100000000 ? f / 1e6 : f > 100000 ? f / 1e3 : f; // Hz / kHz / MHz
            out << mhz;
        }
    }
    CFRelease(prop);
    return out;
}

void PowerMonitor::loadFrequencyTables() {
    io_iterator_t it = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleARMIODevice"), &it) != KERN_SUCCESS)
        return;
    io_object_t o;
    while ((o = IOIteratorNext(it))) {
        io_name_t name;
        if (IORegistryEntryGetName(o, name) == KERN_SUCCESS && std::strcmp(name, "pmgr") == 0) {
            eFreqs_ = dvfsTable(o, "voltage-states1-sram");
            pFreqs_ = dvfsTable(o, "voltage-states5-sram");
            gFreqs_ = dvfsTable(o, "voltage-states9");
        }
        IOObjectRelease(o);
    }
    IOObjectRelease(it);
}

PowerMonitor::PowerMonitor() {
    notify_register_check("com.apple.system.thermalpressurelevel", &notifyToken_);
    loadFrequencyTables();

    CFMutableDictionaryRef energy = IOReportCopyChannelsInGroup(CFSTR("Energy Model"), nullptr, 0, 0, 0);
    CFMutableDictionaryRef cpu = IOReportCopyChannelsInGroup(CFSTR("CPU Stats"),
                                                             CFSTR("CPU Core Performance States"), 0, 0, 0);
    CFMutableDictionaryRef gpu = IOReportCopyChannelsInGroup(CFSTR("GPU Stats"),
                                                             CFSTR("GPU Performance States"), 0, 0, 0);
    if (!energy) return;
    if (cpu) IOReportMergeChannels(energy, cpu, nullptr);
    if (gpu) IOReportMergeChannels(energy, gpu, nullptr);
    CFMutableDictionaryRef chans = CFDictionaryCreateMutableCopy(kCFAllocatorDefault,
                                                                 CFDictionaryGetCount(energy), energy);
    CFRelease(energy);
    if (cpu) CFRelease(cpu);
    if (gpu) CFRelease(gpu);

    CFMutableDictionaryRef subbed = nullptr;
    IOReportSubscriptionRef sub = IOReportCreateSubscription(nullptr, chans, &subbed, 0, nullptr);
    if (!sub || !subbed) {
        CFRelease(chans);
        return;
    }
    channels_ = chans;
    sub_ = sub;
    subbed_ = subbed;
    prev_ = (void *)IOReportCreateSamples(sub, subbed, nullptr);
    clock_.start();
}

PowerMonitor::~PowerMonitor() {
    if (prev_) CFRelease((CFTypeRef)prev_);
    if (subbed_) CFRelease((CFTypeRef)subbed_);
    if (channels_) CFRelease((CFTypeRef)channels_);
    if (notifyToken_ >= 0) notify_cancel(notifyToken_);
    // The subscription object is owned by IOReport; nothing to release.
}

// Weighted average frequency + active ratio of one DVFS residency channel.
static void residencyFreq(CFDictionaryRef item, const QVector<double> &freqs,
                          double &freqOut, double &activeOut, double &totalOut) {
    freqOut = 0;
    activeOut = 0;
    totalOut = 0;
    const int n = IOReportStateGetCount(item);
    int offset = -1;
    QVector<double> res(n);
    for (int i = 0; i < n; ++i) {
        res[i] = double(IOReportStateGetResidency(item, i));
        totalOut += res[i];
        QString nm = qs(IOReportStateGetNameForIndex(item, i));
        if (offset < 0 && nm != "IDLE" && nm != "OFF" && nm != "DOWN") offset = i;
    }
    if (offset < 0 || freqs.isEmpty()) return;
    for (int i = offset; i < n; ++i) activeOut += res[i];
    if (activeOut <= 0) return;
    for (int i = offset; i < n; ++i) {
        int fi = std::min(i - offset, int(freqs.size()) - 1);
        freqOut += res[i] / activeOut * freqs[fi];
    }
}

PowerSample PowerMonitor::sample() {
    PowerSample s = last_;
    uint64_t level = 0;
    if (notifyToken_ >= 0 && notify_get_state(notifyToken_, &level) == NOTIFY_STATUS_OK)
        s.pressure = int(std::min<uint64_t>(level, 4));

    if (!sub_) {
        s.valid = false;
        last_ = s;
        return s;
    }
    const double dt = clock_.isValid() ? clock_.restart() / 1000.0 : 0;
    CFDictionaryRef cur = IOReportCreateSamples((IOReportSubscriptionRef)sub_,
                                                (CFMutableDictionaryRef)subbed_, nullptr);
    if (!cur) return s;
    CFDictionaryRef delta = prev_ ? IOReportCreateSamplesDelta((CFDictionaryRef)prev_, cur, nullptr) : nullptr;
    if (prev_) CFRelease((CFTypeRef)prev_);
    prev_ = (void *)cur;
    if (!delta || dt <= 0.05) {
        if (delta) CFRelease(delta);
        return s;
    }

    double cpuJ = 0, gpuJ = 0, aneJ = 0, dramJ = 0;
    double eF = 0, eA = 0, eT = 0, pF = 0, pA = 0, pT = 0, gF = 0, gA = 0, gT = 0;
    CFArrayRef items = (CFArrayRef)CFDictionaryGetValue(delta, CFSTR("IOReportChannels"));
    const CFIndex count = items ? CFArrayGetCount(items) : 0;
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef item = (CFDictionaryRef)CFArrayGetValueAtIndex(items, i);
        const QString group = qs(IOReportChannelGetGroup(item));
        const QString name = qs(IOReportChannelGetChannelName(item));
        if (group == "Energy Model") {
            const QString unit = qs(IOReportChannelGetUnitLabel(item));
            double v = double(IOReportSimpleGetIntegerValue(item, 0));
            double joules = unit == "mJ" ? v / 1e3 : unit == "uJ" ? v / 1e6 : unit == "nJ" ? v / 1e9 : v;
            if (name.endsWith("CPU Energy")) cpuJ += joules;
            else if (name == "GPU Energy") gpuJ += joules;
            else if (name.startsWith("ANE")) aneJ += joules;
            else if (name.startsWith("DRAM")) dramJ += joules;
        } else if (group == "CPU Stats") {
            double f, a, t;
            if (name.startsWith("ECPU")) {
                residencyFreq(item, eFreqs_, f, a, t);
                eF += f * a; eA += a; eT += t;
            } else if (name.startsWith("PCPU")) {
                residencyFreq(item, pFreqs_, f, a, t);
                pF += f * a; pA += a; pT += t;
            }
        } else if (group == "GPU Stats" && name == "GPUPH") {
            double f, a, t;
            residencyFreq(item, gFreqs_, f, a, t);
            gF += f * a; gA += a; gT += t;
        }
    }
    CFRelease(delta);

    s.valid = true;
    s.cpuW = cpuJ / dt;
    s.gpuW = gpuJ / dt;
    s.aneW = aneJ / dt;
    s.dramW = dramJ / dt;
    s.totalW = s.cpuW + s.gpuW + s.aneW + s.dramW;
    s.eFreqMHz = eA > 0 ? eF / eA : 0;
    s.pFreqMHz = pA > 0 ? pF / pA : 0;
    s.gpuFreqMHz = gA > 0 ? gF / gA : 0;
    s.eUsage = eT > 0 ? eA * 100.0 / eT : 0;
    s.pUsage = pT > 0 ? pA * 100.0 / pT : 0;
    s.gpuUsage = gT > 0 ? gA * 100.0 / gT : 0;
    last_ = s;
    return s;
}

QString PowerMonitor::pressureName(int level) {
    switch (level) {
    case 0: return QStringLiteral("Nominal");
    case 1: return QStringLiteral("Moderate");
    case 2: return QStringLiteral("Heavy");
    case 3: return QStringLiteral("Trapping");
    default: return QStringLiteral("Sleeping");
    }
}

QString PowerMonitor::pressureDescription(int level) {
    switch (level) {
    case 0: return QStringLiteral("No throttling — running at full speed");
    case 1: return QStringLiteral("Light thermal pressure — minor limits possible");
    case 2: return QStringLiteral("Throttling — performance is being reduced to control heat");
    case 3: return QStringLiteral("Severe throttling — heavy performance limits");
    default: return QStringLiteral("Critical — the system is forcing a slowdown");
    }
}
