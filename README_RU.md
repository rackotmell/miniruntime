# MiniRuntime

[English version](README.md)

> **Примечание:** Код написан вручную. ИИ использовался для ревью, подготовки базы doxygen-комментариев и как современный инструмент для поиска информации.

Мини-библиотека для асинхронного выполнения задач на современном C++20 (Linux).

## Обзор

MiniRuntime — мини-фреймворк для асинхронного выполнения задач на C++20. Предоставляет набор компонентов для построения многопоточных приложений с использованием примитивов Linux (epoll, eventfd, timerfd).

Проект реализован для изучения/углубления в детали работы паттернов многопоточной разработки, lock-free структур данных, event-driven архитектур и т.д.

## Компоненты

| Компонент | Заголовок | Описание |
|-----------|-----------|----------|
| **BoundedBlockingQueue** | `boundedblockingqueue.h` | Потокобезопасная ограниченная блокирующая очередь (мьютекс + 2 condition_variable). Блокирует при push когда очередь полна, блокирует при pop когда пуста. Поддерживает операции с таймаутом. |
| **MichaelScottQueue** | `michaelscottqueue.h` | Неограниченная блокирующая FIFO-очередь (Michael & Scott, 1996). Использует hazard pointers для безопасного освобождения памяти. |
| **HazardPointers** | `hazardpointers.h` | Потокобезопасный сборщик мусора для структур без блокировок. Защищает указатели перед разыменованием, обеспечивает безопасное освобождение. |
| **DynamicThreadPool** | `dynamicthreadpool.h` | Пул потоков с автоматической настройкой размера (min/max), таймаутом простоя для завершения потоков и настраиваемой очередью задач. |
| **EventLoop** | `eventloop.h` | Реактор на основе Linux epoll. Поддерживает события raw fd, триггеры eventfd, одноразовые и интервальные таймеры timerfd. |
| **Handle** | `handle.h` | RAII-владельцы регистраций event-loop. Move-only дескрипторы, автоматически отменяющие регистрацию fd при уничтожении. |
| **TaskScheduler** | `taskscheduler.h` | Высокоуровневый фасад: связывает EventLoop с ThreadPool. API: `execute` (немедленно), `schedule` (с задержкой), `scheduleInterval` (периодически). |
| **Future / Promise** | `future.h` | Кастомная реализация без мьютексов, использует `std::atomic` + `wait/notify_all`. Поддерживает result, exception и void-специализации. |
| **SharedValue** | `sharedvalue.h` | Многоразовый контейнер значений для повторяющихся задач (интервалы). Потокобезопасный, перезаписывает предыдущее значение при каждом `set()`. |
| **Logger** | `logger.h` | Асинхронный логгер-синглтон на отдельном потоке. Использует `std::format` + `std::source_location`. Предоставляет макросы `LOG_DEBUG/INFO/WARNING/ERROR`. |

## Технологии

- **C++20**: perfect forwarding, concepts, move-семантика, `std::function`, `jthread`, `atomic::wait/notify`, `std::format`, `std::variant` и т.д.
- **POSIX/Linux**: epoll, timerfd, eventfd
- **Паттерны**: RAII, type erasure (`std::function`), PIMPL, Singleton, Factory method, Facade

## Структура проекта

```
miniruntime/
├── include/           # Публичные заголовочные файлы (публичный интерфейс)
│   └── detail/        # Внутренняя реализация части заголовочных файлов
├── src/               # Реализация библиотеки
├── examples/          # Примеры использования
└── tests/             # Юнит-тесты с использованием GoogleTest
```

## Документация

Документация API предоставлена в виде **комментариев в стиле Doxygen** непосредственно в заголовочных файлах. Каждый публичный класс, метод и важный параметр документирован тегами `@brief`, `@param`, `@return` и `@throws`.

Просматривайте заголовки в `include/` для полной справки по API.

## Сборка

### Требования

- Компилятор с поддержкой C++20 (GCC 10+ или Clang 12+)
- CMake 3.20+
- Linux (epoll/timerfd/eventfd — специфичны для Linux)

### Команды сборки

```bash
cmake -B build && cmake --build build
```

### Опции CMake

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `ENABLE_ASAN_UBSAN` | OFF | Сборка с AddressSanitizer + UndefinedBehaviorSanitizer |
| `ENABLE_TSAN` | OFF | Сборка с ThreadSanitizer |
| `BUILD_EXAMPLES` | ON | Сборка примеров |
| `BUILD_TESTS` | ON | Сборка юнит-тестов (GoogleTest загружается через FetchContent) |

### Санитайзеры

ASAN+UBSAN и TSAN не могут использоваться совместнно:

```bash
# ASAN + UBSAN
cmake -B build-ausan -DENABLE_ASAN_UBSAN=ON && cmake --build build-ausan/  

# TSAN only
cmake -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan/
```

## Запуск тестов

После сборки:

```bash
ctest --test-dir build/tests --output-on-failure
```

Набор тестов включает:

- **Компонентные тесты** — юнит-тесты для каждого компонента (`BoundedBlockingQueue`, `MichaelScottQueue`, `DynamicThreadPool`, `EventLoop`, `Future`, `Logger`, `SharedValue`, `TaskScheduler`)
- **Нагрузочные тесты** — стресс-тесты для проверки поведения при нагрузке (`loadtest`)
- **Тесты совместимости шаблонов** — тесты корректной работы шаблонов с различными типами (`templatecompatibilitytest`)

## Лицензия

Это учебный проект. Используйте по своему усмотрению.
