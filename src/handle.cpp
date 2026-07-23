#include "eventloop.h"

#include <sys/types.h>
#include <unistd.h>
#include <utility>


namespace miniruntime {

    HandleBase::HandleBase(HandleBase&& handle)
        : m_loop(std::exchange(handle.m_loop, nullptr))
        , m_fd(std::exchange(handle.m_fd, -1))
    {}

    HandleBase& HandleBase::operator=(HandleBase&& handle)
    {
        m_loop = std::exchange(handle.m_loop, nullptr);
        m_fd = std::exchange(handle.m_fd, -1);

        return *this;
    }

    HandleBase::HandleBase(EventLoop* loop, int fd)
        : m_loop(loop), m_fd(fd)
    {}

    HandleBase::~HandleBase()
    {
        if (valid())
            m_loop->unregisterEvent(m_fd);
    }

    bool HandleBase::valid() const
    {
        return m_loop && m_fd >= 0;
    }

    EventHandle::EventHandle(EventLoop* loop, int fd) : HandleBase(loop, fd)
    {

    }

    TriggerHandle::TriggerHandle(EventLoop* loop, int fd) : HandleBase(loop, fd)
    {

    }

    void TriggerHandle::trigger() const
    {
        uint64_t one = 1;
        write(m_fd, &one, sizeof(one));
    }
}