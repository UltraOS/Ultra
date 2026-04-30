#define MSG_FMT(msg) "hypervisor: " msg

#include <arch/private/hypervisor.h>
#include <arch/private/linker.h>
#include <arch/private/cpu.h>

#include <common/string.h>

#include <free_after_init.h>
#include <log.h>
#include <linker.h>

static struct active_hypervisor s_hypervisor;
static char s_hypervisor_signature[13];

extern const struct hypervisor SECTION_ARRAY_BEGIN(HYPERVISORS_SECTION)[];
extern const struct hypervisor SECTION_ARRAY_END(HYPERVISORS_SECTION)[];

/*
 * A placeholder hypervisor that is used in case we detect the presence of a
 * hypervisor, but don't know which one it is specifically.
 */
HYPERVISOR s_unknown_hypervisor = {
    .name = "unknown/unsupported",
    .type = HYPERVISOR_TYPE_UNKNOWN,
};

static u32 INIT_CODE find_hypervisor_cpuid_base(
    const struct hypervisor *hv, u32 *out_max_leaf
)
{
    u32 base;
    struct cpuid_res res;
    char signature[12];

    for (base = BASE_HYPERVISOR_LEAF; base <= hv->max_leaf; base += 256) {
        cpuid(base, &res);

        memcpy(signature, &res.b, sizeof(res.b));
        memcpy(signature + sizeof(res.b), &res.c, sizeof(res.c));
        memcpy(signature + sizeof(res.b) + sizeof(res.c), &res.d,
               sizeof(res.d));

        /*
         * If we got at least some sort of a signature, store it aside so we
         * can print it out later for visiblity/debug reasons in case we don't
         * detect/support this hypervisor.
         */
        if (signature[0] && !s_hypervisor_signature[0])
            memcpy(s_hypervisor_signature, signature, sizeof(signature));

        if (memcmp(hv->cpuid_signature, signature, sizeof(signature)) != 0)
            continue;

        if (hv->min_num_leaves != 0) {
            if (res.a < base)
                continue;

            if ((res.a - base) < hv->min_num_leaves)
                continue;
        }

        *out_max_leaf = res.a;
        return base;
    }

    return 0;
}

void INIT_CODE x86_detect_hypervisor(void)
{
    bool cpuid_has_hypervisor;
    const struct hypervisor *hv, *best_hv = nullptr;
    u32 max_leaf = 0, max_priority = 0;

    cpuid_has_hypervisor = all_cpus_have(X86_FEATURE_HYPERVISOR);

    for (hv = SECTION_ARRAY_BEGIN(HYPERVISORS_SECTION);
         hv < SECTION_ARRAY_END(HYPERVISORS_SECTION); ++hv) {
        u32 this_priority = 0;

        if (cpuid_has_hypervisor && hv->max_leaf >= BASE_HYPERVISOR_LEAF)
            this_priority = find_hypervisor_cpuid_base(hv, &max_leaf);

        if (this_priority == 0 && hv->detect)
            this_priority = hv->detect();

        if (this_priority > max_priority)
            best_hv = hv;
    }

    if (!best_hv) {
        if (!cpuid_has_hypervisor) {
            pr_info("running on bare metal\n");
            return;
        }

        /*
         * We know for sure we're inside a hypervisor, but don't know which
         * one it is. Use our unknown placeholder.
         */
        best_hv = &s_unknown_hypervisor;
    }

    s_hypervisor.cpuid_base = max_priority;
    s_hypervisor.max_leaf = max_leaf;

    // Copy into our global since all hypervisor structures are freed later
    memcpy(&s_hypervisor.ops, best_hv, sizeof(s_hypervisor.ops));
    s_hypervisor.present = true;

    if (hypervisor_is(HYPERVISOR_TYPE_UNKNOWN) && s_hypervisor_signature[0]) {
        pr_info(
            "%s (%s)\n", s_hypervisor.ops.name,
            s_hypervisor_signature
        );
    } else {
        pr_info("detected %s\n", s_hypervisor.ops.name);
    }

    if (s_hypervisor.ops.setup)
        s_hypervisor.ops.setup(&s_hypervisor);
}

bool in_hypervisor(void)
{
    return s_hypervisor.present;
}

bool hypervisor_supports(enum hypervisor_feature features)
{
    return (s_hypervisor.features & features) == features;
}

bool hypervisor_is(enum hypervisor_type type)
{
    if (!s_hypervisor.present)
        return false;

    return s_hypervisor.ops.type == type;
}
