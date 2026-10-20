#include "hidsensors.h"

#include <CoreFoundation/CoreFoundation.h>

// --- Private IOHIDEventSystemClient API (present in IOKit, not in headers) ---
typedef struct __IOHIDEvent *IOHIDEventRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
typedef struct __IOHIDEventSystemClient *IOHIDEventSystemClientRef;

extern "C" {
IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
void IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client,
                                       CFDictionaryRef matching);
CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef client);
CFTypeRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef service,
                                         CFStringRef key);
IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef service,
                                          int64_t type, int32_t options,
                                          int64_t timestamp);
double IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);
}

// kIOHIDEventTypeTemperature == 15; its primary field is (type << 16).
static const int64_t kTempEventType = 15;
static const int32_t kTempField = (int32_t)(kTempEventType << 16);
// Temperature sensors match PrimaryUsagePage 0xff00 / PrimaryUsage 5.
static const int kUsagePage = 0xff00;
static const int kUsage = 5;

HidThermal::HidThermal() {
    IOHIDEventSystemClientRef client =
        IOHIDEventSystemClientCreate(kCFAllocatorDefault);
    if (!client) return;
    client_ = client;

    CFNumberRef page = CFNumberCreate(nullptr, kCFNumberIntType, &kUsagePage);
    CFNumberRef usage = CFNumberCreate(nullptr, kCFNumberIntType, &kUsage);
    const void *keys[] = {CFSTR("PrimaryUsagePage"), CFSTR("PrimaryUsage")};
    const void *vals[] = {page, usage};
    CFDictionaryRef match =
        CFDictionaryCreate(nullptr, keys, vals, 2,
                           &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    IOHIDEventSystemClientSetMatching(client, match);

    CFArrayRef services = IOHIDEventSystemClientCopyServices(client);
    if (services) {
        CFIndex n = CFArrayGetCount(services);
        for (CFIndex i = 0; i < n; ++i) {
            IOHIDServiceClientRef s =
                (IOHIDServiceClientRef)CFArrayGetValueAtIndex(services, i);
            CFStringRef name =
                (CFStringRef)IOHIDServiceClientCopyProperty(s, CFSTR("Product"));
            if (!name) continue;
            char buf[256] = {0};
            if (CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                CFRetain(s);                        // keep the service alive
                services_.insert(QString::fromUtf8(buf), (void *)s);
            }
            CFRelease(name);
        }
        CFRetain(services);
        // We retained each service; release the enumerating array.
        CFRelease(services);
    }
    CFRelease(match);
    CFRelease(page);
    CFRelease(usage);
}

HidThermal::~HidThermal() {
    for (void *s : services_)
        if (s) CFRelease((CFTypeRef)s);
    services_.clear();
    if (client_) CFRelease((CFTypeRef)client_);
}

QHash<QString, double> HidThermal::readAll() const {
    QHash<QString, double> out;
    for (auto it = services_.constBegin(); it != services_.constEnd(); ++it) {
        IOHIDServiceClientRef s = (IOHIDServiceClientRef)it.value();
        IOHIDEventRef ev = IOHIDServiceClientCopyEvent(s, kTempEventType, 0, 0);
        if (!ev) continue;
        double v = IOHIDEventGetFloatValue(ev, kTempField);
        CFRelease(ev);
        out.insert(it.key(), v);
    }
    return out;
}
