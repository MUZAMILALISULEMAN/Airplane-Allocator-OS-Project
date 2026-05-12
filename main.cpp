#include "flight.h"
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

double run_data_parallel(const std::vector<Flight>&, std::vector<Flight>*);
double run_task_parallel(const std::vector<Flight>&, std::vector<Flight>*);

static int read_int(const char* prompt, int def) {
    char buf[64];
    printf("%s [%d]: ", prompt, def);
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return def;
    if (buf[0] == '\n' || buf[0] == 0)   return def;
    return atoi(buf);
}

static void add_flight_manual(std::vector<Flight>& v) {
    printf("\n--- Add Flight ---\n");
    printf("Priority codes: 0=EMERGENCY  1=VIP  2=REGULAR\n");
    int prio = read_int("Priority", PRIO_REGULAR);
    if (prio < 0 || prio > 2) prio = PRIO_REGULAR;

    printf("Operation codes: 0=TAKEOFF  1=LANDING\n");
    int op = read_int("Operation", OP_TAKEOFF);
    if (op != OP_LANDING) op = OP_TAKEOFF;

    int svc = read_int("Service time in ms", 150);
    if (svc < 1)    svc = 1;
    if (svc > 5000) svc = 5000;

    Flight f{};
    f.id         = (int)v.size() + 1;
    f.priority   = prio;
    f.operation  = op;
    f.service_ms = svc;
    v.push_back(f);
    printf("Added flight #%d (%s %s svc=%d ms)\n",
           f.id, prio_name(f.priority), op_name(f.operation), f.service_ms);
}

static void generate_random(std::vector<Flight>& v) {
    int n    = read_int("How many random flights", 10);
    int seed = read_int("Random seed", 42);
    if (n < 1)    n = 1;
    if (n > 1000) n = 1000;
    srand((unsigned)seed);
    int start = (int)v.size();
    for (int i = 0; i < n; i++) {
        Flight f{};
        f.id = start + i + 1;
        int r = rand() % 100;
        if      (r < 10) f.priority = PRIO_EMERGENCY;
        else if (r < 30) f.priority = PRIO_VIP;
        else             f.priority = PRIO_REGULAR;
        f.operation  = (rand() % 2) ? OP_TAKEOFF : OP_LANDING;
        f.service_ms = 50 + rand() % 150;
        v.push_back(f);
    }
    printf("Generated %d flights (total now: %zu).\n", n, v.size());
}

static void load_default_scenario(std::vector<Flight>& v) {
    v.clear();
    struct { int prio, op, svc; } scen[] = {
        {PRIO_REGULAR,   OP_TAKEOFF, 200},
        {PRIO_REGULAR,   OP_LANDING, 180},
        {PRIO_VIP,       OP_TAKEOFF, 150},
        {PRIO_REGULAR,   OP_LANDING, 220},
        {PRIO_EMERGENCY, OP_LANDING, 100},
        {PRIO_REGULAR,   OP_TAKEOFF, 190},
        {PRIO_REGULAR,   OP_LANDING, 130},
        {PRIO_EMERGENCY, OP_TAKEOFF,  80},
        {PRIO_VIP,       OP_LANDING, 170},
        {PRIO_REGULAR,   OP_TAKEOFF, 160},
    };
    int n = (int)(sizeof(scen)/sizeof(scen[0]));
    for (int i = 0; i < n; i++) {
        Flight f{};
        f.id         = i + 1;
        f.priority   = scen[i].prio;
        f.operation  = scen[i].op;
        f.service_ms = scen[i].svc;
        v.push_back(f);
    }
    printf("Loaded default scenario (%d flights, 2 emergencies).\n", n);
}

static void list_flights(const std::vector<Flight>& v) {
    if (v.empty()) { printf("\n(No flights queued.)\n"); return; }
    printf("\n--- Flight list (%zu) ---\n", v.size());
    printf(" ID  PRIORITY    OPERATION  SERVICE(ms)\n");
    printf("---  ----------  ---------  -----------\n");
    for (const auto& f : v) {
        printf("%3d  %s   %s    %4d\n",
               f.id, prio_name(f.priority), op_name(f.operation), f.service_ms);
    }
}

static double run_sequential(const std::vector<Flight>& flights,
                             std::vector<Flight>* results_out) {
    std::vector<Flight> work = flights;
    std::sort(work.begin(), work.end(), [](const Flight& a, const Flight& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.id < b.id;
    });
    if (results_out) results_out->clear();
    long t0 = now_us();
    for (auto& f : work) {
        f.enqueued_us = t0;
        f.started_us  = now_us();
        f.runway      = (f.operation == OP_TAKEOFF) ? 1 : 2;
        printf("[Sequential] flight %d (%s %s) svc=%d ms\n",
               f.id, prio_name(f.priority), op_name(f.operation), f.service_ms);
        fflush(stdout);
        usleep(f.service_ms * 1000);
        f.finished_us = now_us();
        if (results_out) results_out->push_back(f);
    }
    long t1 = now_us();
    return (t1 - t0) / 1000.0;
}

static void print_results_table(const char* title,
                                std::vector<Flight> r,
                                long t0_us) {
    std::sort(r.begin(), r.end(), [](const Flight& a, const Flight& b) {
        return a.id < b.id;
    });
    printf("\n--- %s: per-flight metrics ---\n", title);
    printf(" ID  PRIORITY    OP       RWY  ENQ(ms)  START(ms)  WAIT(ms)  SVC(ms)  FIN(ms)\n");
    printf("---  ----------  -------  ---  -------  ---------  --------  -------  -------\n");
    long total_wait = 0;
    for (const auto& f : r) {
        long enq   = (f.enqueued_us - t0_us) / 1000;
        long start = (f.started_us  - t0_us) / 1000;
        long fin   = (f.finished_us - t0_us) / 1000;
        long wait  = (f.started_us  - f.enqueued_us) / 1000;
        total_wait += wait;
        printf("%3d  %s   %s    %d    %5ld     %6ld     %5ld     %4d     %5ld\n",
               f.id, prio_name(f.priority), op_name(f.operation), f.runway,
               enq, start, wait, f.service_ms, fin);
    }
    if (!r.empty()) {
        printf("Avg wait: %ld ms\n", total_wait / (long)r.size());
    }
}

static void print_summary(double seq_ms, double data_ms, double task_ms) {
    printf("\n=========================================\n");
    printf(" PERFORMANCE SUMMARY\n");
    printf("=========================================\n");
    printf(" Sequential      : %8.2f ms\n", seq_ms);
    printf(" Data parallel   : %8.2f ms  (speedup vs seq: %.2fx)\n",
           data_ms, seq_ms / data_ms);
    printf(" Task parallel   : %8.2f ms  (speedup vs seq: %.2fx)\n",
           task_ms, seq_ms / task_ms);
    printf(" Faster of two   : %s\n",
           data_ms < task_ms ? "Data parallelism (pthread)"
                             : "Task parallelism (fork+mmap)");
    printf("=========================================\n");
}

static void run_simulation(const std::vector<Flight>& flights) {
    if (flights.empty()) {
        printf("\nNo flights to simulate. Add some first.\n");
        return;
    }
    printf("\n=========================================\n");
    printf(" Simulating %zu flights across 3 modes\n", flights.size());
    printf("=========================================\n");

    std::vector<Flight> seq_results, data_results, task_results;

    printf("\n[ Mode 1/3 ] Sequential baseline\n");
    long seq_t0 = now_us();
    double seq_ms = run_sequential(flights, &seq_results);

    printf("\n[ Mode 2/3 ] Data parallelism (pthread + mutex + cond var)\n");
    long data_t0 = now_us();
    double data_ms = run_data_parallel(flights, &data_results);

    printf("\n[ Mode 3/3 ] Task parallelism (fork + mmap + POSIX sem)\n");
    long task_t0 = now_us();
    double task_ms = run_task_parallel(flights, &task_results);

    print_results_table("Sequential",       seq_results,  seq_t0);
    print_results_table("Data parallelism", data_results, data_t0);
    print_results_table("Task parallelism", task_results, task_t0);
    print_summary(seq_ms, data_ms, task_ms);
}

static void print_menu() {
    printf("\n========== Airplane Allocator ==========\n");
    printf(" 1) Add flight manually\n");
    printf(" 2) Generate N random flights\n");
    printf(" 3) Load default scenario (10 flights)\n");
    printf(" 4) View flight list\n");
    printf(" 5) Clear all flights\n");
    printf(" 6) Run simulation\n");
    printf(" 7) Quit\n");
    printf("========================================\n");
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::vector<Flight> flights;
    printf("Welcome to the Airplane Allocator.\n");
    printf("Operating Systems CS-2006 project demo.\n");

    while (true) {
        print_menu();
        int choice = read_int("Choice", 0);
        switch (choice) {
            case 1: add_flight_manual(flights); break;
            case 2: generate_random(flights); break;
            case 3: load_default_scenario(flights); break;
            case 4: list_flights(flights); break;
            case 5: flights.clear(); printf("Flight list cleared.\n"); break;
            case 6: run_simulation(flights); break;
            case 7: printf("Goodbye.\n"); return 0;
            default: printf("Invalid choice.\n"); break;
        }
    }
}
