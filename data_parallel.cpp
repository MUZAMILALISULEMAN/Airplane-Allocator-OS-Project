#include "flight.h"
#include <pthread.h>
#include <unistd.h>
#include <queue>
#include <vector>
#include <chrono>
#include <cstdio>

struct FlightCmp {
    bool operator()(const Flight& a, const Flight& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id > b.id;
    }
};

struct ThreadQueue {
    std::priority_queue<Flight, std::vector<Flight>, FlightCmp> pq;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    bool closed;
};

static void tq_init(ThreadQueue* q) {
    pthread_mutex_init(&q->mtx, nullptr);
    pthread_cond_init(&q->cv, nullptr);
    q->closed = false;
}

static void tq_destroy(ThreadQueue* q) {
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cv);
}

static void tq_push(ThreadQueue* q, Flight f) {
    f.enqueued_us = now_us();
    pthread_mutex_lock(&q->mtx);
    q->pq.push(f);
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

static void tq_close(ThreadQueue* q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = true;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

static bool tq_pop(ThreadQueue* q, Flight* out) {
    pthread_mutex_lock(&q->mtx);
    while (q->pq.empty() && !q->closed) {
        pthread_cond_wait(&q->cv, &q->mtx);
    }
    if (q->pq.empty()) {
        pthread_mutex_unlock(&q->mtx);
        return false;
    }
    *out = q->pq.top();
    q->pq.pop();
    pthread_mutex_unlock(&q->mtx);
    return true;
}

struct ResultBag {
    std::vector<Flight> items;
    pthread_mutex_t mtx;
};

struct WorkerArg {
    ThreadQueue* q;
    ResultBag*   results;
    const char*  runway;
    int          runway_id;
};

static void* runway_worker(void* arg) {
    WorkerArg* w = (WorkerArg*)arg;
    Flight f;
    while (tq_pop(w->q, &f)) {
        f.started_us = now_us();
        f.runway     = w->runway_id;
        printf("[%s tid=%lu] START   flight %d (%s %s) svc=%d ms\n",
               w->runway, (unsigned long)pthread_self(),
               f.id, prio_name(f.priority), op_name(f.operation), f.service_ms);
        fflush(stdout);

        usleep(f.service_ms * 1000);

        f.finished_us = now_us();
        printf("[%s tid=%lu] FINISH  flight %d  wait=%ld ms\n",
               w->runway, (unsigned long)pthread_self(),
               f.id, (f.started_us - f.enqueued_us) / 1000);
        fflush(stdout);

        pthread_mutex_lock(&w->results->mtx);
        w->results->items.push_back(f);
        pthread_mutex_unlock(&w->results->mtx);
    }
    return nullptr;
}

double run_data_parallel(const std::vector<Flight>& flights,
                        std::vector<Flight>* results_out) {
    ThreadQueue takeoff_q, landing_q;
    tq_init(&takeoff_q);
    tq_init(&landing_q);

    ResultBag results;
    pthread_mutex_init(&results.mtx, nullptr);

    WorkerArg a1 = { &takeoff_q, &results, "Runway1-Takeoff", 1 };
    WorkerArg a2 = { &landing_q, &results, "Runway2-Landing", 2 };

    auto t0 = std::chrono::steady_clock::now();

    pthread_t t1, t2;
    pthread_create(&t1, nullptr, runway_worker, &a1);
    pthread_create(&t2, nullptr, runway_worker, &a2);

    for (const auto& f : flights) {
        if (f.operation == OP_TAKEOFF) tq_push(&takeoff_q, f);
        else                            tq_push(&landing_q, f);
    }
    tq_close(&takeoff_q);
    tq_close(&landing_q);

    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);

    auto t1c = std::chrono::steady_clock::now();

    if (results_out) *results_out = std::move(results.items);

    pthread_mutex_destroy(&results.mtx);
    tq_destroy(&takeoff_q);
    tq_destroy(&landing_q);

    return std::chrono::duration<double, std::milli>(t1c - t0).count();
}
