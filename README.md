<div align="center">

# ✈️ Airplane Allocator

### A parallelism-driven airport runway scheduler

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue?style=flat-square&logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%2F%20Unix-orange?style=flat-square&logo=linux)](https://www.kernel.org/)
[![Threads](https://img.shields.io/badge/Threads-POSIX%20pthreads-green?style=flat-square)](https://man7.org/linux/man-pages/man7/pthreads.7.html)
[![IPC](https://img.shields.io/badge/IPC-fork%20%2B%20mmap%20%2B%20sem__t-purple?style=flat-square)](https://man7.org/linux/man-pages/man2/mmap.2.html)
[![Course](https://img.shields.io/badge/Course-CS--2006%20Operating%20Systems-red?style=flat-square)](https://www.nu.edu.pk/)

> Simulates priority-based aircraft scheduling across two runways using **three execution modes** — Sequential, Data Parallelism (pthreads), and Task Parallelism (fork + mmap) — and benchmarks them side-by-side.

</div>

---

## 📑 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [Execution Modes](#-execution-modes)
- [Architecture](#-architecture)
- [Project Structure](#-project-structure)
- [Getting Started](#-getting-started)
- [Usage](#-usage)
- [Results](#-results)
- [Synchronization Mechanisms](#-synchronization-mechanisms)
- [Team](#-team)

---

## 🗺 Overview

**Airplane Allocator** is a C++17 simulation that models an airport with two runways:

| Runway | Handles |
|--------|---------|
| **Runway 1** | Takeoffs |
| **Runway 2** | Landings |

Flights are categorized by priority and always processed in this order:

```
🚨 EMERGENCY  →  👔 VIP  →  🧳 REGULAR
```

The same workload is run through all three modes so their performance can be measured and compared directly. Emergency flights inserted mid-queue are immediately moved to the front — achieving **0 ms wait time** in all modes.

---

## ✨ Features

- **Priority-based scheduling** via a min-heap (`EMERGENCY < VIP < REGULAR`)
- **Three execution strategies** on identical workloads for a fair benchmark
- **Interactive CLI** — add flights manually, generate random loads, or run a built-in 10-flight scenario
- **Per-flight metrics** — queue time, start time, wait time, service time, completion time
- **Speed-up summary** printed after every simulation run
- Zero busy-waiting — threads block on condition variables; processes block on semaphores

---

## ⚙️ Execution Modes

### 1 · Sequential
A single thread processes all flights in priority order. Used as the **baseline** for speed-up calculations.

### 2 · Data Parallelism — `data_parallel.cpp`
Two **POSIX threads** (one per runway) share the same address space.

```
Synchronization primitives used:
  pthread_mutex_t  →  protect the priority queue & results vector
  pthread_cond_t   →  block consumer threads when queue is empty;
                       signal on push, broadcast on close
```

### 3 · Task Parallelism — `task_parallel.cpp`
Two **child processes** created with `fork()`. Each process gets its own address space, so the shared priority queue lives in an `mmap(MAP_SHARED | MAP_ANONYMOUS)` region.

```
Synchronization primitives used:
  sem_t (binary,   pshared=1)  →  mutex protecting the shared heap
  sem_t (counting, pshared=1)  →  tracks pending flights;
                                   workers block on sem_wait when empty
```

---

## 🏗 Architecture

```
                    ┌─────────────────────┐
                    │       main.cpp      │
                    │   (menu + driver)   │
                    └────────┬────────────┘
                             │  flights[]
              ┌──────────────┴──────────────┐
              │                             │
              ▼                             ▼
  ┌─────────────────────┐     ┌──────────────────────┐
  │  data_parallel.cpp  │     │  task_parallel.cpp   │
  │  pthread + mutex    │     │  fork + mmap +        │
  │  + condition var    │     │  POSIX sem_t          │
  └──────┬──────────────┘     └────────┬─────────────┘
         │                             │
   pthread threads               forked processes
  ┌──────┴──────┐             ┌────────┴────────┐
  │  Runway 1  │  Runway 2   │  Runway 1       │  Runway 2
  │  Thread    │  Thread     │  Process        │  Process
  └────────────┘─────────────└─────────────────┘─────────
       │              │              │                 │
  std::priority_queue           mmap shared binary heap
  + pthread_mutex_t             + sem_t (pshared=1)
  + pthread_cond_t
```

---

## 📁 Project Structure

```
os-project/
├── flight.h              # Flight struct, priority enums, now_us() timestamp helper
├── data_parallel.cpp     # Thread-based parallel execution (pthread)
├── task_parallel.cpp     # Process-based parallel execution (fork + mmap + sem)
├── main.cpp              # CLI menu, sequential baseline, results printer
└── Makefile              # Build rules: make / make run / make clean
```

### Key Data Structures

**`Flight` struct** — carries all scheduling and metrics fields:
```cpp
struct Flight {
    int  id;
    int  priority;       // 0 = EMERGENCY | 1 = VIP | 2 = REGULAR
    int  operation;      // 0 = TAKEOFF (Runway 1) | 1 = LANDING (Runway 2)
    int  service_ms;     // simulated runway occupation time
    long enqueued_us;    // timestamp: added to queue
    long started_us;     // timestamp: runway began servicing
    long finished_us;    // timestamp: flight cleared the runway
    int  runway;         // which runway processed this flight
};
```

**`FlightCmp` comparator** — drives the min-heap ordering:
```cpp
struct FlightCmp {
    bool operator()(const Flight& a, const Flight& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id > b.id;   // tie-break on ID for determinism
    }
};
```

> **Note:** The data-parallel module uses `std::priority_queue<Flight, vector<Flight>, FlightCmp>`.  
> The task-parallel module uses a hand-written **POD binary heap** in shared memory, since STL containers cannot be safely placed in `mmap`-shared regions across `fork()`.

---

## 🚀 Getting Started

### Prerequisites

- Linux or any POSIX-compliant OS
- GCC / G++ with C++17 support (`g++ --version`)
- POSIX threads and semaphore libraries (standard on Linux)

### Build

```bash
git clone https://github.com/<your-username>/airplane-allocator.git
cd airplane-allocator
make
```

This produces the `./allocator` executable.

```bash
make clean   # remove build artifacts
make run     # build + run in one step
```

---

## 💻 Usage

### Interactive Mode

```bash
./allocator
```

```
╔════════════════════════════════════╗
║      AIRPLANE ALLOCATOR v1.0       ║
╚════════════════════════════════════╝

 1) Add flight manually
 2) Generate N random flights
 3) Load default scenario (10 flights)
 4) View flight list
 5) Clear all flights
 6) Run simulation
 7) Quit
```

### Non-Interactive (Scripted) Demo

```bash
# Load the default 10-flight scenario, run all three modes, then quit
printf '3\n6\n7\n' | ./allocator
```

### Manual Flight Entry

When adding a flight manually you specify:
- **Priority** — `0` Emergency / `1` VIP / `2` Regular  
- **Operation** — `0` Takeoff / `1` Landing  
- **Service time** — simulated runway occupation in milliseconds

---

## 📊 Results

### Speed-up Summary (default 10-flight scenario)

| Mode | Wall-clock Time | Speed-up |
|------|-----------------|----------|
| Sequential | 1610.07 ms | 1.00× |
| **Data Parallelism** (pthread) | **810.93 ms** | **1.99×** |
| **Task Parallelism** (fork + mmap) | **816.75 ms** | **1.97×** |

Both parallel modes approach the theoretical **2× ceiling** — the maximum achievable with two independent runways.

---

### Per-Flight Metrics (Data Parallelism)

| ID | Priority | Op | Rwy | Wait (ms) | Svc (ms) | Fin (ms) |
|----|----------|----|-----|-----------|----------|----------|
| 5  | 🚨 EMERGENCY | LANDING | 2 | **0** | 100 | 101 |
| 8  | 🚨 EMERGENCY | TAKEOFF | 1 | **0** | 80 | 84 |
| 3  | 👔 VIP | TAKEOFF | 1 | 83 | 150 | 239 |
| 9  | 👔 VIP | LANDING | 2 | 101 | 170 | 276 |
| 1  | 🧳 REGULAR | TAKEOFF | 1 | 239 | 200 | 441 |
| 2  | 🧳 REGULAR | LANDING | 2 | 276 | 180 | 459 |
| … | … | … | … | … | … | … |

**Avg wait (parallel): 291 ms** vs **663 ms (sequential)** → **>55% reduction**

Key observations:
- ✅ Both emergency flights achieved **0 ms wait** despite being inserted after regular flights
- ✅ VIP flights were served immediately after emergencies cleared
- ✅ Task parallelism is only ~6 ms slower than thread-based — within OS scheduling noise

---

## 🔒 Synchronization Mechanisms

| Primitive | Module | Role |
|-----------|--------|------|
| `pthread_mutex_t` | Data Parallel | Exclusive access to the priority queue and results vector |
| `pthread_cond_t` | Data Parallel | Block consumers when queue is empty; wake on push/close |
| `sem_t` *(binary, pshared=1)* | Task Parallel | Binary mutex protecting the shared-memory heap |
| `sem_t` *(counting, pshared=1)* | Task Parallel | Tracks pending flights; workers block via `sem_wait` |

Both modules guarantee **at most one flight is processed on a given runway at any time**, mirroring real-world runway exclusivity.

---

## 👥 Team

| Name | Student ID |
|------|-----------|
| **Muzamil Ali** | 24K-1023 |
| **Muzammil Zaidi** | 24K-0887 |

---
