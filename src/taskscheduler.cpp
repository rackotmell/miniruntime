#include "taskscheduler.h"
#include <cstddef>

namespace miniruntime {

    TaskScheduler::TaskScheduler(size_t minParallelTasks, size_t maxParallelTasks)
        : m_pool(minParallelTasks, maxParallelTasks)
        , m_loopThread([this]{ m_loop.run(); })
    {
        
    }

}