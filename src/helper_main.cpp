// helper_main.cpp — tiny privileged helper for SMC fan writes.
//
// Installed once (setuid root) at:
//   /Library/Application Support/MacFans/macfans-helper
// After that the GUI calls it to change fan speed WITHOUT a password prompt.
//
// Safety: it only ever writes keys that start with 'F' (fan keys), so it
// cannot be used to poke arbitrary SMC registers. No Qt, no dynamic config —
// a deliberately small attack surface for a setuid binary.
//
// Usage:
//   macfans-helper version                  prints the helper protocol version
//   macfans-helper write <KEY> <hexbytes>   e.g. write F0Tg 44F0C800 (flt)
//   macfans-helper mode  <KEY> <0|1>        e.g. mode F0Md 1
//   macfans-helper watch <PID>              crash watchdog: waits for PID to
//                                           exit, then hands every fan back to
//                                           macOS (F*Md = 0) and exits
#include "smc.h"

#include <signal.h>
#include <sys/event.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char *kHelperVersion = "3";

static int parseHex(const std::string &hex, uint8_t *out, uint32_t cap) {
    if (hex.size() % 2 != 0) return -1;
    uint32_t n = 0;
    for (size_t i = 0; i + 1 < hex.size() && n < cap; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        char *end = nullptr;
        long v = strtol(b, &end, 16);
        if (end != b + 2) return -1;
        out[n++] = (uint8_t)v;
    }
    return (int)n;
}

// Return every fan to automatic (system) control. Writes only F<n>Md keys.
static void restoreAllFans() {
    smc::Connection c;
    if (!c.open()) return;
    smc::Value v;
    int n = c.read("FNum", v) ? (int)std::lround(v.toDouble()) : 0;
    for (int i = 0; i < n && i < 16; ++i) {
        char key[5];
        snprintf(key, sizeof(key), "F%dMd", i);
        uint8_t zero = 0;
        c.write(key, &zero, 1);
    }
}

// Block until `pid` exits (kqueue NOTE_EXIT), then restore automatic control.
static int watchProcess(pid_t pid) {
    if (pid <= 1) return 2;
    setsid();                 // detach from the app's session / terminal
    signal(SIGHUP, SIG_IGN);
    int kq = kqueue();
    if (kq < 0) return 7;
    struct kevent ev;
    EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (kevent(kq, &ev, 1, nullptr, 0, nullptr) == 0) {
        struct kevent out;
        while (kevent(kq, nullptr, 0, &out, 1, nullptr) < 0) { /* EINTR: retry */ }
    }
    // Either it exited, or it was already gone when we started watching.
    restoreAllFans();
    close(kq);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "version") {
        printf("%s\n", kHelperVersion);
        return 0;
    }
    // Adopt full root identity granted by the setuid bit.
    if (setuid(0) != 0 && geteuid() != 0) {
        fprintf(stderr, "macfans-helper: not privileged\n");
        return 10;
    }
    if (argc == 3 && std::string(argv[1]) == "watch")
        return watchProcess((pid_t)atoi(argv[2]));

    if (argc < 4) {
        fprintf(stderr, "usage: macfans-helper version | watch <PID> | write|mode <KEY> <arg>\n");
        return 2;
    }

    std::string cmd = argv[1];
    std::string key = argv[2];
    if (key.size() != 4 || key[0] != 'F') {
        fprintf(stderr, "macfans-helper: only fan (F*) keys are allowed\n");
        return 3;
    }

    smc::Connection c;
    if (!c.open()) {
        fprintf(stderr, "macfans-helper: cannot open SMC\n");
        return 4;
    }

    if (cmd == "write") {
        uint8_t buf[32];
        int n = parseHex(argv[3], buf, sizeof(buf));
        if (n <= 0) {
            fprintf(stderr, "macfans-helper: bad hex\n");
            return 5;
        }
        return c.write(key, buf, (uint32_t)n) ? 0 : 6;
    }
    if (cmd == "mode") {
        uint8_t b = (std::string(argv[3]) == "1") ? 1 : 0;
        return c.write(key, &b, 1) ? 0 : 6;
    }
    fprintf(stderr, "macfans-helper: unknown command\n");
    return 2;
}
