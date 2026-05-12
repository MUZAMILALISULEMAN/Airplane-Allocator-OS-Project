# Operating Systems (CS-2006) — Spring 2026
# Project Report

## Airplane Allocator using Parallelism Techniques

**Group Members**
- Muzamil Ali — 24K-1023
- Syed Muhammad Muzammil Zaidi — 24K-0887

**Advisor**
- Mr. Ubaidullah

**National University of Computer & Emerging Sciences**

---

## 1. Overview

The **Airplane Allocator** is a simulation system that manages aircraft takeoffs and
landings on two runways using **parallelism and inter-process synchronization**
techniques from the operating system. The simulator implements three execution
strategies for the same workload so that their behavior and performance can be
directly compared:

1. **Sequential** — a single thread serves all flights in priority order.
2. **Data Parallelism** — two POSIX **pthread** threads, one per runway, share
   the same address space and serve their queues concurrently. Synchronization
   uses `pthread_mutex_t` and `pthread_cond_t`.
3. **Task Parallelism** — two child processes created with `fork()` serve the
   runways concurrently. They communicate through a **shared-memory region**
   created with `mmap(MAP_SHARED | MAP_ANONYMOUS, ...)` and synchronize using
   **POSIX semaphores** (`sem_t` initialized with `pshared = 1`).

The system aims to:
- Manage airplane scheduling efficiently using a **priority queue** so that
  emergency aircraft are serviced before VIP and regular flights.
- Demonstrate **mutex locks**, **condition variables**, and **counting
  semaphores** as different solutions to the same synchronization problem.
- Quantify the speed-up obtained by parallel execution against a sequential
  baseline.

---

## 2. Project Objectives

| # | Objective (from proposal) | Status |
|---|---|---|
| 1 | Efficient airplane scheduling via priority queues | Achieved (min-heap on priority) |
| 2 | Data parallelism — dedicated threads for takeoff and landing | Achieved (pthread workers per runway) |
| 3 | Task parallelism — asynchronous tasks for takeoff/landing | Achieved (fork+mmap workers per runway) |
| 4 | Resource management — mutex locks + condition variables | Achieved in data-parallel module |
| 5 | Performance comparison between the two techniques | Achieved (sequential baseline included) |

In addition to the proposed objectives the implementation also covers:

- **Inter-process synchronization** using POSIX semaphores in shared memory.
- An **interactive command-line interface** for adding flights manually, loading
  test scenarios, generating random workloads, and running the simulator.
- A **per-flight metrics report** (queue arrival time, start time, waiting
  time, service time, completion time) that proves the priority queue actually
  preempts regular flights when an emergency arrives.

---

## 3. Methodology

### 3.1 Data Structures and Flight Management

The `Flight` structure (in `flight.h`) carries every attribute needed both for
scheduling and for performance measurement:

```cpp
struct Flight {
    int  id;
    int  priority;       // 0 = EMERGENCY, 1 = VIP, 2 = REGULAR
    int  operation;      // 0 = TAKEOFF (Runway 1), 1 = LANDING (Runway 2)
    int  service_ms;     // simulated runway occupation time
    long enqueued_us;    // timestamp when added to the runway queue
    long started_us;     // timestamp when runway began servicing the flight
    long finished_us;    // timestamp when the flight cleared the runway
    int  runway;         // which runway processed the flight (1 or 2)
};
```

A **comparator** is used to order flights in the priority queue: smaller
`priority` is served first, and ties break on `id` so the ordering is
deterministic.

```cpp
struct FlightCmp {
    bool operator()(const Flight& a, const Flight& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id > b.id;
    }
};
```

The data-parallel module uses `std::priority_queue<Flight, std::vector<Flight>,
FlightCmp>`. The task-parallel module uses a hand-written **binary heap** on a
fixed-size array because the queue lives in `mmap()`-shared memory and must be
POD-friendly (C++ STL containers cannot be safely shared across `fork()`).

### 3.2 Runway Management

The proposal calls for two runways:
- **Runway 1** — handles takeoffs.
- **Runway 2** — handles landings.

Each runway owns its **own priority queue**, so an emergency landing never
waits behind a regular takeoff and vice versa. In the data-parallel module each
runway is driven by a dedicated worker thread; in the task-parallel module each
runway is driven by a dedicated child process.

### 3.3 Parallelism Techniques

#### Data Parallelism (file: `data_parallel.cpp`)

Two **POSIX threads** are created with `pthread_create`. Each thread runs the
function `runway_worker`, which loops on a thread-safe priority queue and
services whichever flight is currently highest priority.

```cpp
pthread_create(&t1, nullptr, runway_worker, &arg_takeoff);
pthread_create(&t2, nullptr, runway_worker, &arg_landing);
```

The threads share the parent process's address space, so the queues and the
results vector are simple in-memory objects guarded by `pthread_mutex_t`. When a
queue is empty the consumer thread blocks on `pthread_cond_wait` rather than
busy-waiting; the producer signals the condition variable on every push and
broadcasts when the queue is closed.

#### Task Parallelism (file: `task_parallel.cpp`)

Two child processes are spawned with `fork()`. Because each process gets a
**separate address space**, the priority queue is placed in a shared-memory
region created before the fork:

```cpp
SharedHeap* h = (SharedHeap*)mmap(
    nullptr, sizeof(SharedHeap),
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
sem_init(&h->mtx,   /*pshared=*/1, /*value=*/1);
sem_init(&h->items, /*pshared=*/1, /*value=*/0);
```

The semaphore `mtx` is used as a **binary mutex** (initial value 1) to protect
the heap from concurrent mutation. The semaphore `items` is a **counting
semaphore** representing the number of pending flights — the parent posts it
after every push, and the worker blocks on `sem_wait(&items)` when the queue
is empty. A `closed` flag plus one final `sem_post(&items)` provides a clean
shutdown signal.

The choice of `fork() + mmap + sem_t` instead of `std::async` makes this module
a meaningful demonstration of **inter-process communication** — a topic
central to the Operating Systems course.

### 3.4 Synchronization

| Mechanism | Used by | Purpose |
|---|---|---|
| `pthread_mutex_t` | Data parallel | Protect the C++ priority queue and results vector |
| `pthread_cond_t` | Data parallel | Block consumer threads while the queue is empty; signal/broadcast on push and on close |
| `sem_t` (binary, pshared) | Task parallel | Protect the shared-memory heap during mutation |
| `sem_t` (counting, pshared) | Task parallel | Track the number of pending flights so workers block efficiently when empty |

Both modules guarantee that at most one flight is processed on a given runway
at a time, which is the real-world constraint demanded by the proposal.

### 3.5 Flight Allocation Logic

- **Emergency priority** — an emergency flight inserted while regular flights
  are pending **immediately** becomes the head of the queue and is the next
  one picked up by the runway. Test runs (Section 6) confirm a wait time of
  `0 ms` for emergencies.
- **VIP priority** — chosen by the queue only after every pending emergency
  has been served.
- **Regular priority** — served in order of arrival, FIFO within their level.

### 3.6 Performance Analysis

The simulator records four wall-clock measurements with
`std::chrono::steady_clock`:

- Total simulation time per execution mode.
- For each flight: enqueue time, start time, finish time, derived waiting
  time.

A per-flight metrics table and a final speed-up summary are printed at the end
of every run. Identical inputs are used for all three modes so the comparison
is fair.

---

## 4. System Architecture

```
                    +---------------------+
                    |        main.cpp     |
                    |   (menu + driver)   |
                    +----+-----------+----+
                         |           |
              flights[]  |           |  flights[]
                         v           v
                +------------------+    +-------------------+
                | data_parallel.cpp|    |  task_parallel.cpp|
                | (pthread + mutex |    |  (fork + mmap +   |
                |  + cond var)     |    |   POSIX sem)      |
                +---------+--------+    +---------+---------+
                          |                       |
                  pthread threads          fork()ed processes
                  +--------+--------+      +------+-----+-----+
                  |Runway1 | Runway2|      |Runway1|   |Runway2|
                  |Thread  | Thread |      |proc.  |   |proc.  |
                  +--------+--------+      +-------+   +-------+
                       |       |               |           |
                  std::pq   std::pq        mmap shared heap
                  + mutex   + mutex        + sem_t (pshared)
                  + cond    + cond
```

---

## 5. Implementation Files

| File | Lines | Purpose |
|---|---|---|
| `flight.h` | ~45 | `Flight` struct, priority/operation enums, `now_us()` helper for time stamps. |
| `data_parallel.cpp` | ~135 | Thread-safe priority queue, `pthread_create`/`join` worker model, mutex + condition-variable synchronization, returns metrics through a results vector. |
| `task_parallel.cpp` | ~150 | Shared-memory binary heap, `mmap` setup, `fork()` worker processes, semaphore-based synchronization, shared results array returned to the parent. |
| `main.cpp` | ~185 | Interactive menu, manual / random / scripted flight entry, sequential baseline, results table printer, performance summary. |
| `Makefile` | 17 | `make`, `make run`, `make clean`. |

---

## 6. Test Run and Results

### 6.1 Default Scenario

The built-in scenario (menu option **3**) contains 10 flights including
**2 emergencies** (one takeoff and one landing). The scenario deliberately
places emergencies *late* in the input order so the queue must reorder them.

| # | Priority | Operation | Service (ms) |
|---|---|---|---|
| 1 | REGULAR   | TAKEOFF | 200 |
| 2 | REGULAR   | LANDING | 180 |
| 3 | VIP       | TAKEOFF | 150 |
| 4 | REGULAR   | LANDING | 220 |
| 5 | EMERGENCY | LANDING | 100 |
| 6 | REGULAR   | TAKEOFF | 190 |
| 7 | REGULAR   | LANDING | 130 |
| 8 | EMERGENCY | TAKEOFF |  80 |
| 9 | VIP       | LANDING | 170 |
| 10| REGULAR   | TAKEOFF | 160 |

### 6.2 Total Wall-clock Time

| Mode | Wall-clock time | Speed-up vs Sequential |
|---|---|---|
| Sequential                      | **1610.07 ms** | 1.00× |
| Data parallelism (pthread)      | **810.93 ms**  | **1.99×** |
| Task parallelism (fork + mmap)  | **816.75 ms**  | **1.97×** |

Both parallel modes reach almost the theoretical 2× ceiling, which is the
maximum achievable with two runways.

### 6.3 Per-Flight Metrics (Data Parallelism)

```
 ID  PRIORITY    OP       RWY  ENQ(ms)  START(ms)  WAIT(ms)  SVC(ms)  FIN(ms)
---  ----------  -------  ---  -------  ---------  --------  -------  -------
  1  REGULAR     TAKEOFF    1        0        239       239      200       441
  2  REGULAR     LANDING    2        0        277       276      180       459
  3  VIP         TAKEOFF    1        0         84        83      150       239
  4  REGULAR     LANDING    2        0        459       459      220       680
  5  EMERGENCY   LANDING    2        0          0         0      100       101
  6  REGULAR     TAKEOFF    1        0        441       441      190       635
  7  REGULAR     LANDING    2        0        680       679      130       810
  8  EMERGENCY   TAKEOFF    1        0          0         0       80        84
  9  VIP         LANDING    2        0        101       101      170       276
 10  REGULAR     TAKEOFF    1        0        635       634      160       796
Avg wait: 291 ms
```

Three observations confirm correctness:

1. Both emergency flights (#5, #8) recorded a waiting time of **0 ms** — the
   priority queue handed them to a runway immediately even though they were
   inserted after several regular flights.
2. VIP flights (#3, #9) were served right after the emergencies.
3. The average waiting time fell from **663 ms** (sequential) to **291 ms**
   (data parallelism), a reduction of more than 55%.

### 6.4 Why Task Parallelism is a Few Milliseconds Slower

The fork-based implementation pays a small one-time cost for `fork()`,
`mmap()`, and semaphore setup. Once running, both workers execute exactly the
same instructions as the thread-based version. The difference (≈ 6 ms out of
~810 ms) is within the noise of OS scheduling and matches the textbook
expectation: **threads are cheaper to create, but processes provide stronger
isolation**.

---

## 7. Expected Outcomes — Achieved

| Outcome from proposal | Demonstrated in this report / project |
|---|---|
| Fully functional airplane allocator | Menu-driven program in `main.cpp` |
| Priority-based scheduling | `FlightCmp`, results table in Section 6.3 |
| Performance comparison data | Sequential vs Data vs Task in Section 6.2 |
| Synchronization understanding | Mutex + condition variable AND counting semaphore both used and explained |
| Multi-threading and concurrency skills | Two parallel implementations in C++17 |

---

## 8. Libraries and Tools Used

| Component | Header / API | Why it was chosen |
|---|---|---|
| Threads | `<pthread.h>` — `pthread_create`, `pthread_join`, `pthread_mutex_t`, `pthread_cond_t` | POSIX standard; required by the proposal |
| Processes & sleep | `<unistd.h>` — `fork`, `usleep`, `getpid`, `_exit` | Standard system calls for IPC and timing |
| Shared memory | `<sys/mman.h>` — `mmap`, `munmap` with `MAP_SHARED \| MAP_ANONYMOUS` | Allows the parent and forked workers to share the priority queue |
| Process synchronization | `<semaphore.h>` — `sem_init(&s, /*pshared=*/1, ...)`, `sem_wait`, `sem_post` | POSIX semaphore usable across processes when placed in shared memory |
| Process wait | `<sys/wait.h>` — `waitpid` | Parent waits for worker processes to finish before measuring elapsed time |
| Timing | `<chrono>` and `clock_gettime(CLOCK_MONOTONIC,...)` | Monotonic, microsecond-precision clock used by both threads and processes |
| Priority queue | `<queue>` and a custom binary heap | `std::priority_queue` for threads; POD heap for shared-memory processes |

---

## 9. How to Build and Run

```bash
cd os-project
make            # produces ./allocator
./allocator     # launches the interactive menu
```

Menu options:

```
 1) Add flight manually
 2) Generate N random flights
 3) Load default scenario (10 flights)
 4) View flight list
 5) Clear all flights
 6) Run simulation
 7) Quit
```

For a non-interactive demo:

```bash
printf '3\n6\n7\n' | ./allocator    # load scenario, run, quit
```

---

## 10. Conclusion

The Airplane Allocator project successfully simulates a real-world airport
scheduling problem and uses it as a vehicle to demonstrate two different
parallelism models supported by modern operating systems. The
**thread-based** implementation showcases shared-memory concurrency using
`pthread` primitives, mutex locks, and condition variables. The
**process-based** implementation uses `fork()`, `mmap`-backed shared memory,
and process-shared POSIX semaphores — primitives that lie at the heart of
inter-process communication on Unix systems.

Across identical 10-flight workloads, both parallel implementations achieved
~1.97–1.99× speed-up over a sequential baseline, very close to the theoretical
2× ceiling for two runways. The per-flight metrics table confirms that the
priority queue correctly preempts regular and VIP flights in favor of
emergencies, with the latter recording a waiting time of zero milliseconds.

The project therefore meets every objective stated in the proposal, and goes
further by adding an interactive user interface and a detailed per-flight
metrics report — both of which make the demonstration more convincing and the
underlying mechanisms easier to inspect during evaluation.
