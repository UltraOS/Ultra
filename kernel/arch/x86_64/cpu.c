#include <arch/private/cpu.h>
#include <arch/private/msr.h>

#include <common/string.h>
#include <common/ctype.h>
#include <common/atomic.h>

#include <free_after_init.h>
#include <panic.h>
#include <per_cpu.h>

struct x86_cpu_info g_cpu_info;
DEFINE_PER_CPU(struct x86_cpu_info, g_this_cpu_info);

static inline char *set_cpu_name_part(char *name, struct cpuid_res *res)
{
    memcpy(name, &res->a, sizeof(res->a));
    name += sizeof(res->a);
    memcpy(name, &res->b, sizeof(res->b));
    name += sizeof(res->b);
    memcpy(name, &res->c, sizeof(res->c));
    name += sizeof(res->c);
    memcpy(name, &res->d, sizeof(res->d));
    name += sizeof(res->d);

    return name;
}

void INIT_CODE cpu_info_setup(struct x86_cpu_info *info)
{
    struct cpuid_res res;

    cpuid(0, &res);

    info->max_cpuid = res.a;
    memcpy(&info->vendor[0], &res.b, sizeof(res.b));
    memcpy(&info->vendor[sizeof(res.b)], &res.d, sizeof(res.d));
    memcpy(&info->vendor[sizeof(res.b) + sizeof(res.d)], &res.c, sizeof(res.c));

    if (info->max_cpuid >= 1) {
        cpuid(1, &res);

        info->feature_dwords[X86_FEATURE_DWORD_1_C] = res.c;
        info->feature_dwords[X86_FEATURE_DWORD_1_D] = res.d;

        info->stepping = res.a & 0x0F;
        info->family = (res.a >> 8) & 0x0F;
        info->model = (res.a >> 4) & 0x0F;

        /*
         * The Extended Family ID needs to be examined only when the Family ID
         * is 0FH.
         *
         * DisplayFamily = Extended_Family_ID + Family_ID;
         */
        if (info->family == 0xF)
            info->family += (res.a >> 20) & 0xFF;

        /*
         * The Extended Model ID needs to be examined only when the Family ID is
         * 06H or 0FH.
         *
         * DisplayModel = (Extended_Model_ID << 4) + Model_ID;
         */
        if (info->family == 0x06 || info->family >= 0x0F)
            info->model += ((res.a >> 16) & 0x0F) << 4;
    }

    if (info->max_cpuid >= 7) {
        cpuid(7, &res);

        info->feature_dwords[X86_FEATURE_DWORD_7_B] = res.b;
        info->feature_dwords[X86_FEATURE_DWORD_7_C] = res.c;
        info->feature_dwords[X86_FEATURE_DWORD_7_D] = res.d;

        /*
         * EAX Bits 31 - 00: Reports the maximum input value for supported
         * leaf 7 sub-leaves.
         */
        if (res.a >= 1) {
            cpuid_subleaf(7, 1, &res);
            info->feature_dwords[X86_FEATURE_DWORD_7_A_1] = res.a;
        }
    }

    if (info->max_cpuid >= 0x0D) {
        cpuid_subleaf(0x0D, 1, &res);
        info->feature_dwords[X86_FEATURE_DWORD_D_A_1] = res.a;
    }

    cpuid(0x8000'0000, &res);
    if (res.a >= 0x8000'0000 && res.a <= 0x8000'FFFF)
        info->max_extended_cpuid = res.a;

    if (info->max_extended_cpuid >= 0x8000'0001) {
        cpuid(0x8000'0001, &res);
        info->feature_dwords[X86_FEATURE_DWORD_8000_0001_C] = res.c;
        info->feature_dwords[X86_FEATURE_DWORD_8000_0001_D] = res.d;
    }

    if (info->max_extended_cpuid >= 0x8000'0004) {
        char *name = info->name;
        u8 i, j;

        cpuid(0x8000'0002, &res);
        name = set_cpu_name_part(name, &res);

        cpuid(0x8000'0003, &res);
        name = set_cpu_name_part(name, &res);

        cpuid(0x8000'0004, &res);
        set_cpu_name_part(name, &res);

        info->name[sizeof(info->name) - 1] = '\0';
        name = info->name;

        while (isspace(*name))
            name++;

        for (i = 0, j = 0; *name; i++, name++) {
            info->name[i] = *name;

            if (!isspace(*name))
                j = i;
        }
        info->name[j + 1] = '\0';
    }

    if (info->max_extended_cpuid >= 0x8000'0007) {
        cpuid(0x8000'0007, &res);
        info->feature_dwords[X86_FEATURE_DWORD_8000_0007_D] = res.d;
    }

    if (info->max_extended_cpuid >= 0x8000'0008) {
        cpuid(0x8000'0008, &res);

        info->feature_dwords[X86_FEATURE_DWORD_8000_0008_B] = res.b;
        info->phys_bits = res.a & 0xFF;
        info->virt_bits = (res.a >> 8) & 0xFF;
    } else {
        info->phys_bits = 36;
        info->virt_bits = 48;
    }

    if (info != &g_cpu_info) {
        int i;

        /*
         * AND our features into the BSP feature table so that it contains the
         * LCD of all CPUs. Use atomic since this function may be executed by
         * all APs at the same time.
         */
        for (i = 0; i < X86_FEATURE_DWORD_COUNT; i++) {
            atomic_and_fetch(
                &g_cpu_info.feature_dwords[i], info->feature_dwords[i],
                MO_ACQ_REL
            );
        }
    }
}

void cpuid_inline_subleaf(
    u32 function, u32 subleaf, void *a, void *b, void *c, void *d
)
{
    u32 *a_dw = a, *b_dw = b, *c_dw = c, *d_dw = d;

    asm volatile("cpuid"
            : "=a"(*a_dw), "=b"(*b_dw), "=c"(*c_dw), "=d"(*d_dw)
            : "a"(function), "c"(subleaf));
}

void cpuid_inline(u32 function, void *a, void *b, void *c, void *d)
{
    cpuid_inline_subleaf(function, 0, a, b, c, d);
}

void cpuid_subleaf(u32 function, u32 subleaf, struct cpuid_res *id)
{
    cpuid_inline_subleaf(
        function, subleaf, &id->a, &id->b, &id->c, &id->d
    );
}

void cpuid(u32 function, struct cpuid_res *id)
{
    cpuid_subleaf(function, 0, id);
}

static void die_on_msr_access_failure(const char *op, u32 msr)
{
    panic("Unable to %s MSR 0x%08X\n", op, msr);
}

u64 rdmsr_or_die(u32 msr)
{
    error_t ret;
    u64 value;

    ret = rdmsr(msr, &value);
    if (is_error(ret))
        die_on_msr_access_failure("read from", msr);

    return value;
}

void wrmsr_or_die(u32 msr, u64 value)
{
    error_t ret;

    ret = wrmsr(msr, value);
    if (is_error(ret))
        die_on_msr_access_failure("write to", msr);
}
