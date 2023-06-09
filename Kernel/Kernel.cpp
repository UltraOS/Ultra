#include "ACPI/ACPI.h"
#include "Common/Conversions.h"
#include "Common/Logger.h"
#include "Common/Types.h"
#include "Core/Boot.h"
#include "Core/CPU.h"
#include "Core/FPU.h"
#include "Core/GDT.h"
#include "Core/Runtime.h"
#include "Drivers/PCI/PCI.h"
#include "Drivers/PS2/PS2Controller.h"
#include "Drivers/Video/VideoDevice.h"
#include "FileSystem/VFS.h"
#include "Interrupts/ExceptionDispatcher.h"
#include "Interrupts/IDT.h"
#include "Interrupts/InterruptController.h"
#include "Interrupts/InterruptManager.h"
#include "Interrupts/SyscallDispatcher.h"
#include "Interrupts/Timer.h"
#include "Memory/AddressSpace.h"
#include "Memory/HeapAllocator.h"
#include "Memory/MemoryManager.h"
#include "Memory/MemoryMap.h"
#include "Multitasking/Scheduler.h"
#include "Multitasking/Sleep.h"
#include "Time/RTC.h"
#include "WindowManager/DemoTTY.h"
#include "WindowManager/Painter.h"
#include "WindowManager/WindowManager.h"

namespace kernel {

[[noreturn]] void process_with_windows();
[[noreturn]] void dummy_thread();
[[noreturn]] void initialize_drivers();

[[noreturn]] void run(LoaderContext* context)
{
    Logger::initialize();
    runtime::ensure_loaded_correctly();
    runtime::KernelSymbolTable::parse_all();

    // Generates native memory map, initializes BootAllocator, initializes kernel heap
    MemoryManager::early_initialize(context);

    runtime::init_global_objects();

    GDT::the().create_basic_descriptors();
    GDT::the().install();

    InterruptManager::initialize_all();
    IDT::the().install();

    MemoryManager::inititalize();

    DeviceManager::initialize();
    ACPI::the().initialize();

    InterruptController::discover_and_setup();
    Timer::discover_and_setup();
    CPU::initialize();
    FPU::detect_features();
    FPU::initialize_for_this_cpu();

    VideoDevice::discover_and_setup(context);

    Scheduler::inititalize();
    CPU::start_all_processors();

    Time::initialize();

    WindowManager::initialize();

    Process::create_supervisor(initialize_drivers, "Driver init");

    {
        // A process with 2 threads
        auto process = Process::create_supervisor(process_with_windows, "demo window"_sv);
        process->create_thread(dummy_thread);
    }

    DemoTTY::initialize();

    Interrupts::enable();

    for (;;)
        hlt();
}

void initialize_drivers()
{
    PS2Controller::detect();

    PCI::the().collect_all_devices();
    PCI::the().initialize_supported();
    VFS::initialize();

    Scheduler::the().exit_process(0);
}

void process_with_windows()
{
    Window* window = Window::create(*Thread::current(), { 10, 10, 200, 100 }, WindowManager::the().active_theme(), "Demo"_sv).get();

    Painter window_painter(&window->surface());

    window_painter.fill_rect(window->view_rect(), { 0x16, 0x21, 0x3e });

    window->invalidate_part_of_view_rect({ 0, 0, 200, 100 });

    while (true) {
        auto event = window->pop_event();

        for (;;) {
            if (event.type == EventType::EMPTY) {
                break;
            } else if (event.type == EventType::WINDOW_SHOULD_CLOSE) {
                window->close();
                Scheduler::the().exit_process(0);
            }

            event = window->pop_event();
        }

        sleep::for_milliseconds(100);
    }
}

void dummy_thread()
{
    for (;;) {
        sleep::for_seconds(1);
    }
}
}
