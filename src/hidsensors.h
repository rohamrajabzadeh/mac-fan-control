// hidsensors.h — Apple thermal sensors via IOHIDEventSystemClient.
//
// Apple Silicon exposes rich, human-named thermal sensors (pACC/eACC CPU cores,
// GPU, ANE neural engine, ISP, SOC, PMGR/PMU power dies, NAND) through the HID
// event system — the same source commercial tools use for names like
// "Neural Engine" or "SOC Sensor". These are NOT the SMC Txxx keys. The
// functions are part of IOKit but not in public headers, so they are declared
// here. No root needed.
#pragma once

#include <QHash>
#include <QString>

class HidThermal {
public:
    HidThermal();
    ~HidThermal();

    bool available() const { return !services_.isEmpty(); }

    // name (e.g. "pACC MTR Temp Sensor2") -> temperature °C, for all sensors.
    QHash<QString, double> readAll() const;

private:
    void *client_ = nullptr;          // IOHIDEventSystemClientRef
    QHash<QString, void *> services_; // name -> IOHIDServiceClientRef
};
