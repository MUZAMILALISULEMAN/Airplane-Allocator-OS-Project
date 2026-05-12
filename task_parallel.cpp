#include "flight.h"
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>

#define MAX_HEAP 4096

struct SharedHeap {
    Flight data[MAX_HEAP];
    int    size;
    int    closed;
    sem_t  mtx;
    sem_t  items;

    Flight results[MAX_HEAP];
    int    results_count;
    sem_t  results_mtx;
};

static int cmp(const Flight& a, const Flight& b) {
    if (a.priority != b.priority) return a.priority < b.priority ? -1 : 1;
    if (a.id != b.id)             return a.id < b.id ? -1 : 1;
    return 0;
}

static void heap_push(SharedHeap* h, const Flight& f) {
    int i = h->size++;
    h->data[i] = f;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (cmp(h->data[i], h->data[parent]) < 0) {
            Flight tmp = h->data[i];
            h->data[i] = h->data[parent];
            h->data[parent] = tmp;
            i = parent;
        } else break;
    }
}

static void heap_pop(SharedHeap* h, Flight* out) {
    *out = h->data[0];
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];
        int i = 0;
        while (true) {
            int l = 2*i + 1, r = 2*i + 2, best = i;
            if (l < h->size && cmp(h->data[l], h->data[best]) < 0) best = l;
            if (r < h->size && cmp(h->data[r], h->data[best]) < 0) best = r;
            if (best == i) break;
            Flight tmp = h->data[i];
            h->data[i] = h->data[best];
            h->data[best] = tmp;
            i = best;
        }
    }
}

static SharedHeap* heap_create() {
    SharedHeap* h = (SharedHeap*)mmap(nullptr, sizeof(SharedHeap),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (h == MAP_FAILED) { perror("mmap"); return nullptr; }
    h->size = 0;
    h->closed = 0;
    h->results_count = 0;
    sem_init(&h->mtx,         1, 1);
    sem_init(&h->items,       1, 0);
    sem_init(&h->results_mtx, 1, 1);
    return h;
}

static void heap_destroy(SharedHeap* h) {
    sem_destroy(&h->mtx);
    sem_destroy(&h->items);
    sem_destroy(&h->results_mtx);
    munmap(h, sizeof(SharedHeap));
}

static void heap_close(SharedHeap* h) {
    sem_wait(&h->mtx);
    h->closed = 1;
    sem_post(&h->mtx);
    sem_post(&h->items);
}

static void runway_process(SharedHeap* h, const char* runway, int runway_id) {
    while (true) {
        sem_wait(&h->items);
        sem_wait(&h->mtx);
        if (h->size == 0) {
            sem_post(&h->mtx);
            break;
        }
        Flight f;
        heap_pop(h, &f);
        sem_post(&h->mtx);

        f.started_us = now_us();
        f.runway     = runway_id;
        printf("[%s pid=%d] START   flight %d (%s %s) svc=%d ms\n",
               runway, getpid(),
               f.id, prio_name(f.priority), op_name(f.operation), f.service_ms);
        fflush(stdout);

        usleep(f.service_ms * 1000);

        f.finished_us = now_us();
        printf("[%s pid=%d] FINISH  flight %d  wait=%ld ms\n",
               runway, getpid(),
               f.id, (f.started_us - f.enqueued_us) / 1000);
        fflush(stdout);

        sem_wait(&h->results_mtx);
        if (h->results_count < MAX_HEAP) {
            h->results[h->results_count++] = f;
        }
        sem_post(&h->results_mtx);
    }
}

double run_task_parallel(const std::vector<Flight>& flights,
                        std::vector<Flight>* results_out) {
    SharedHeap* takeoff_h = heap_create();
    SharedHeap* landing_h = heap_create();
    if (!takeoff_h || !landing_h) return -1.0;

    auto t0 = std::chrono::steady_clock::now();

    pid_t p1 = fork();
    if (p1 == 0) {
        runway_process(takeoff_h, "Runway1-Takeoff", 1);
        _exit(0);
    }
    pid_t p2 = fork();
    if (p2 == 0) {
        runway_process(landing_h, "Runway2-Landing", 2);
        _exit(0);
    }

    for (auto f : flights) {
        f.enqueued_us = now_us();
        SharedHeap* h = (f.operation == OP_TAKEOFF) ? takeoff_h : landing_h;
        sem_wait(&h->mtx);
        heap_push(h, f);
        sem_post(&h->mtx);
        sem_post(&h->items);
    }

    heap_close(takeoff_h);
    heap_close(landing_h);

    waitpid(p1, nullptr, 0);
    waitpid(p2, nullptr, 0);

    auto t1c = std::chrono::steady_clock::now();

    if (results_out) {
        results_out->clear();
        for (int i = 0; i < takeoff_h->results_count; i++)
            results_out->push_back(takeoff_h->results[i]);
        for (int i = 0; i < landing_h->results_count; i++)
            results_out->push_back(landing_h->results[i]);
    }

    heap_destroy(takeoff_h);
    heap_destroy(landing_h);
    return std::chrono::duration<double, std::milli>(t1c - t0).count();
}
