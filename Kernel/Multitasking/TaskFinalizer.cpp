#include "TaskFinalizer.h"
#include "Core/FPU.h"
#include "FileSystem/VFS.h"
#include "Memory/MemoryManager.h"
#include "Process.h"
#include "Sleep.h"
#include "WindowManager/Window.h"

namespace kernel {

TaskFinalizer* TaskFinalizer::s_instance;

TaskFinalizer::TaskFinalizer()
    : m_threads_to_free(new List<Thread>())
    , m_threads_being_freed(new List<Thread>())
{
}

void TaskFinalizer::spawn()
{
    Process::create_supervisor(&TaskFinalizer::run, "TaskFinalizer");
}

void TaskFinalizer::run()
{
    ASSERT(s_instance == nullptr);
    s_instance = new TaskFinalizer();

    Thread::current()->set_invulnerable(true);

    bool did_work = false;

    for (;;) {
        did_work = s_instance->finalize_task_queue();

        // TODO: Maybe add some sort of thread blocking if there are no tasks to free
        if (!did_work)
            sleep::for_seconds(1);
    }
}

void TaskFinalizer::swap_queues()
{
    LOCK_GUARD(m_threads_to_free_list_lock);
    swap(m_threads_to_free, m_threads_being_freed);
}

void TaskFinalizer::free_thread(Thread& thread)
{
    LOCK_GUARD(m_threads_to_free_list_lock);
    m_threads_to_free->insert_back(thread);
}

bool TaskFinalizer::finalize_task_queue()
{
    ASSERT(m_threads_being_freed->empty());
    swap_queues();

    bool freed_at_least_one = false;

    while (!m_threads_being_freed->empty()) {
        freed_at_least_one = true;
        auto& next_thread_to_free = m_threads_being_freed->pop_front();
        do_free_thread(next_thread_to_free);
    }

    while (!m_processes_to_free.empty()) {
        freed_at_least_one = true;
        auto next_process_to_free = m_processes_to_free.front();
        m_processes_to_free.pop_front();

        do_free_process(move(next_process_to_free));
    }

    return freed_at_least_one;
}

void TaskFinalizer::do_free_thread(Thread& thread)
{
    log() << "TaskFinalizer: Freeing thread \"" << thread.id()
          << "\" of process \"" << thread.owner().name().to_view() << "\"...";

    auto& owner = thread.owner();

    auto& windows = thread.windows();
    while (!windows.empty()) {
        auto this_window = windows.begin();
        this_window->second->close();
    }

    if (thread.fpu_state())
        FPU::free_state(thread.fpu_state());

    MemoryManager::the().free_virtual_region(thread.kernel_stack());

    {
        LOCK_GUARD(owner.lock());
        auto& threads = owner.threads();
        threads.remove(thread.id()); // thread gets freed here

        // We're done here, there are still some threads this process owns
        if (!owner.threads().empty())
            return;
    }

    // Process doesn't have any more threads attached to it, we can free it
    m_processes_to_free.append_back(Scheduler::the().unregister_process(owner.id()));
}

void TaskFinalizer::do_free_process(RefPtr<Process>&& process)
{
    log() << "TaskFinalizer: Freeing process \"" << process->name().to_view() << "\"";

    for (auto& stream : process->io_streams())
        stream.second->close();

    MemoryManager::the().free_all_virtual_regions(*process);

    if (process->is_supervisor() == IsSupervisor::NO)
        MemoryManager::the().free_address_space(process->address_space());

    process->mark_as_released();
    process.reset(); // drop the refcount to 0
}

}
