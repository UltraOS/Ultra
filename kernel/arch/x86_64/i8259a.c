#define MSG_FMT(msg) "i8259a: " msg

#include <memory/io.h>
#include <common/bit.h>
#include <arch/private/vectors.h>

#include <arch/private/i8259a.h>

#include <bug.h>
#include <free_after_init.h>
#include <log.h>

#define I8259A_MASTER 0x20
#define I8259A_SLAVE 0xA0

// ICW1 is sent with A0 = 0
#define I8259A_ICW1 BIT_U8(4)
#define I8259A_ICW1_ICW4_NEEDED BIT_U8(0)

#define I8259A_NUM_IR_PINS 8

// The IR pin the slave PIC is attached to in master
#define I8259A_PC_AT_SLAVE_IR 2

#define I8259A_ICW4_8086_MODE BIT_U8(0)

static void INIT_CODE i8259a_cmd(io_window *iow, bool a0, u8 data)
{
    iowrite8_slowdown(iow, a0, data);
}

static void INIT_CODE i8259a_set_imr(io_window *iow, u8 mask)
{
    i8259a_cmd(iow, true, mask);
}

static void INIT_CODE i8259a_program_one(
    io_window *iow, u8 vector_base, u8 icw3
)
{
    /*
     * Cleared SNGL (bit 1) for cascade mode, cleared LTIM (3) for edge
     * triggering
     */
    i8259a_cmd(iow, false, I8259A_ICW1 | I8259A_ICW1_ICW4_NEEDED);
    i8259a_cmd(iow, true, vector_base);
    i8259a_cmd(iow, true, icw3);

    /*
     * Cleared AEOI (bit 1) for normal EOI, cleared BUF (3) for non-buffered
     * mode, cleared SFNM (4) for "not special fully nested mode"
     */
    i8259a_cmd(iow, true, I8259A_ICW4_8086_MODE);

    // Mask all interrupts
    i8259a_set_imr(iow, 0xFF);
}

void INIT_CODE i8259a_quiesce(void)
{
    io_window master, slave;

    BUG_ON(is_error(io_window_map_pio(&master, I8259A_MASTER, 2)));
    BUG_ON(is_error(io_window_map_pio(&slave, I8259A_SLAVE, 2)));

    i8259a_program_one(
        &master, VECTOR_DYNAMIC_FIRST,
        // Master needs a bit set for each IR pin that has a slave attached
        BIT_U8(I8259A_PC_AT_SLAVE_IR)
    );
    i8259a_program_one(
        &slave, VECTOR_DYNAMIC_FIRST + I8259A_NUM_IR_PINS,
        // The slave needs to know which IR it's attached to
        I8259A_PC_AT_SLAVE_IR
    );

    io_window_unmap(&master);
    io_window_unmap(&slave);

    pr_info("remapped & masked\n");
}
