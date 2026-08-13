# MiniRuntime

[README на русском](README_RU.md)

> **Note:** The code was written manually. AI was used for code review, preparing the base of Doxygen comments, and as a modern tool for information retrieval.

A mini-runtime library for asynchronous task execution in modern C++20 (Linux).

## Overview

MiniRuntime — a mini-framework for asynchronous task execution in C++20. It provides a set of composable components for building concurrent applications using Linux-specific primitives (epoll, eventfd, timerfd).

This project was designed to explore and gain a deeper understanding of the inner workings of multithreading patterns, lock-free data structures, event-driven architectures, and so on.

## Components

| Component | Header | Namespace | Description |
|-----------|--------|-----------|-------------|
| **BoundedBlockingQueue** | `miniruntime/task/boundedblockingqueue.h` | `miniruntime::task` | Thread-safe bounded blocking queue (mutex + 2 condition variables). Blocks on push when full, blocks on pop when empty. Supports timeout operations. |
| **MichaelScottQueue** | `miniruntime/task/michaelscottqueue.h` | `miniruntime::task` | Unbounded lock-free FIFO queue (Michael & Scott, 1996). Uses hazard pointers for safe memory reclamation. |
| **HazardPointers** | `miniruntime/task/hazardpointers.h` | `miniruntime::task` | Thread-safe garbage collector for lock-free structures. Protects pointers before dereferencing, ensures safe deallocation. |
| **DynamicThreadPool** | `miniruntime/task/dynamicthreadpool.h` | `miniruntime::task` | Thread pool with automatic size adjustment (min/max), idle-timeout thread retirement, and configurable task queue. |
| **EventLoop** | `miniruntime/event/eventloop.h` | `miniruntime::event` | Linux epoll-driven event reactor. Supports raw fd events, eventfd triggers, one-shot and interval timerfd timers. |
| **Handle** | `miniruntime/event/handle.h` | `miniruntime::event` | RAII owners of event-loop registrations. Move-only handles that automatically unregister fds on destruction. |
| **TaskScheduler** | `miniruntime/scheduler/taskscheduler.h` | `miniruntime::scheduler` | High-level facade: connects EventLoop to ThreadPool. API: `execute` (immediate), `schedule` (delayed), `scheduleInterval` (recurring). |
| **Future / Promise** | `miniruntime/asyncresult/future.h` | `miniruntime::asyncresult` | Custom implementation without mutexes, using `std::atomic` + `wait/notify_all`. Supports result, exception, and void specializations. |
| **SharedValue** | `miniruntime/asyncresult/sharedvalue.h` | `miniruntime::asyncresult` | Reusable value holder for repeating tasks (intervals). Thread-safe, overwrites previous value on each `set()`. |
| **Logger** | `miniruntime/logger/logger.h` | `miniruntime::logger` | Asynchronous singleton logger on a separate thread. Uses `std::format` + `std::source_location`. Provides `LOG_DEBUG/INFO/WARNING/ERROR` macros. |

## Technologies

- **C++20**: perfect forwarding, concepts, move-semantic, `std::function`, `jthread`, `atomic::wait/notify`, `std::format`, `std::variant`, etc.
- **POSIX/Linux**: epoll, timerfd, eventfd
- **Паттерны**: RAII, type erasure (`std::function`), PIMPL, Singleton, Factory method, Facade

## Project Structure

```
miniruntime/
├── include/miniruntime/
│   ├── miniruntime.h              # Umbrella header (includes all components)
│   ├── event/                     # EventLoop, Handle
│   ├── asyncresult/               # Future, Promise, SharedValue
│   ├── task/                      # DynamicThreadPool, queues, HazardPointers
│   ├── scheduler/                 # TaskScheduler
│   └── logger/                    # Logger
├── src/
│   ├── event/
│   ├── asyncresult/
│   ├── task/
│   ├── scheduler/
│   └── logger/
├── examples/                      # Usage examples
└── tests/
    ├── event/
    ├── asyncresult/
    ├── task/
    ├── scheduler/
    └── logger/
```

## Documentation

API documentation is provided as **Doxygen-style comments** directly in the header files. Each public class, method, and important parameter is documented with `@brief`, `@param`, `@return`, and `@throws` tags.

Browse the headers in `include/miniruntime/` for full API reference. You can also use the umbrella header `#include <miniruntime/miniruntime.h>` to include all components at once.

## Building

### Prerequisites

- C++20 compatible compiler (GCC 10+ or Clang 12+)
- CMake 3.20+
- Linux (epoll/timerfd/eventfd are Linux-specific)

### Build Commands

```bash
cmake -B build && cmake --build build
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_ASAN_UBSAN` | OFF | Build with AddressSanitizer + UndefinedBehaviorSanitizer |
| `ENABLE_TSAN` | OFF | Build with ThreadSanitizer |
| `BUILD_EXAMPLES` | ON | Build example programs |
| `BUILD_TESTS` | ON | Build unit tests (GoogleTest fetched via FetchContent) |

### Sanitizers

ASAN+UBSAN and TSAN are mutually exclusive:

```bash
# ASAN + UBSAN
cmake -B build-ausan -DENABLE_ASAN_UBSAN=ON && cmake --build build-ausan/  

# TSAN only
cmake -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan/
```

## Running Tests

After building:

```bash
ctest --test-dir build/tests --output-on-failure
```

The test suite includes:

- **Component tests** — unit tests for each component (`BoundedBlockingQueue`, `MichaelScottQueue`, `DynamicThreadPool`, `EventLoop`, `Future`, `Logger`, `SharedValue`, `TaskScheduler`)
- **Load tests** — stress tests to verify behavior under concurrent load (`loadtest`)
- **Template compatibility tests** — tests to ensure templates work correctly with different types (`templatecompatibilitytest`)

## License

This is an educational project. Use as you see fit.
