// powermon.h — Apple silicon power, clock speeds and thermal pressure, without
// root. Power comes from IOReport "Energy Model" counters (energy delta / time),
// clocks from per-core DVFS residencies weighted by the chip's own frequency
// tables (pmgr voltage-states), and throttling state from the system thermal
// pressure level (the same signal powermetrics reports).
#pragma once

#include <QString>
#include <QVector>
#include <QElapsedTimer>

struct PowerSample {
    bool valid = false;        // power / clocks available on this Mac
    double cpuW = 0, gpuW = 0, aneW = 0, dramW = 0, totalW = 0;
    double eFreqMHz = 0, pFreqMHz = 0, gpuFreqMHz = 0;
    double eUsage = 0, pUsage = 0, gpuUsage = 0;   // % active residency
    int pressure = 0;          // 0 Nominal, 1 Moderate, 2 Heavy, 3 Trapping, 4 Sleeping
};

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();
    PowerSample sample();      // deltas since the previous call
    double maxPFreqMHz() const { return pFreqs_.isEmpty() ? 0 : pFreqs_.last(); }
    double maxEFreqMHz() const { return eFreqs_.isEmpty() ? 0 : eFreqs_.last(); }
    double maxGpuFreqMHz() const { return gFreqs_.isEmpty() ? 0 : gFreqs_.last(); }

    static QString pressureName(int level);
    static QString pressureDescription(int level);

private:
    void loadFrequencyTables();

    void *sub_ = nullptr;          // IOReportSubscriptionRef
    void *subbed_ = nullptr;       // CFMutableDictionaryRef
    void *channels_ = nullptr;     // CFMutableDictionaryRef
    void *prev_ = nullptr;         // CFDictionaryRef (previous samples)
    QElapsedTimer clock_;
    QVector<double> eFreqs_, pFreqs_, gFreqs_;
    int notifyToken_ = -1;
    PowerSample last_;
};
