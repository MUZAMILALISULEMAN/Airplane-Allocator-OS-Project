#pragma once

#include <time.h>

enum Priority {
    PRIO_EMERGENCY = 0,
    PRIO_VIP       = 1,
    PRIO_REGULAR   = 2,
};

enum Operation {
    OP_TAKEOFF = 0,
    OP_LANDING = 1,
};

struct Flight {
    int  id;
    int  priority;
    int  operation;
    int  service_ms;
    long enqueued_us;
    long started_us;
    long finished_us;
    int  runway;
};

inline const char* prio_name(int p) {
    switch (p) {
        case PRIO_EMERGENCY: return "EMERGENCY";
        case PRIO_VIP:       return "VIP      ";
        case PRIO_REGULAR:   return "REGULAR  ";
        default:             return "?        ";
    }
}

inline const char* op_name(int o) {
    return o == OP_TAKEOFF ? "TAKEOFF" : "LANDING";
}

inline long now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}
