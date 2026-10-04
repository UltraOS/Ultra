#define MSG_FMT(msg) "fireworks: " msg

#include <common/types.h>
#include <common/atomic.h>
#include <common/bit.h>
#include <common/minmax.h>

#include <boot/boot.h>
#include <memory/io.h>
#include <memory/alloc.h>
#include <time/time.h>
#include <time/units.h>
#include <sched/sched.h>
#include <sched/kthread.h>

#include <log.h>
#include <param.h>
#include <smp.h>
#include <module.h>
#include <free_after_init.h>

#define FP_ONE ((i64)1 << 16)

#define FRAMES_PER_SEC 60
#define SIGNED_MS_PER_SEC ((i64)MS_PER_SEC)
#define STATS_PERIOD_MS 2000
#define DRAIN_POLL_PERIOD_MS 100

// A dot is the screen height divided by this, but at least one pixel
#define DOTS_PER_SCREEN_HEIGHT 384

/*
 * Speeds are in percent of the screen width or height per second, and gravity
 * is in percent of the screen height per second squared.
 */
#define GRAVITY_PCT 5

#define ROCKETS_PER_VOLLEY_MIN 1
#define ROCKETS_PER_VOLLEY_MAX 20
#define VOLLEY_INTERVAL_MIN_MS 2000
#define VOLLEY_INTERVAL_MAX_MS 4000

#define ROCKET_SIDEWAYS_SPEED_MAX_PCT 40
#define ROCKET_RISE_SPEED_MIN_PCT 50
#define ROCKET_RISE_SPEED_MAX_PCT 100
#define ROCKET_LIFETIME_MIN_MS 500
#define ROCKET_LIFETIME_MAX_MS 1000

#define PARTICLES_PER_ROCKET_MIN 100
#define PARTICLES_PER_ROCKET_MAX 200
#define PARTICLE_SPEED_MIN_PCT 12
#define PARTICLE_SPEED_MAX_PCT 25
#define PARTICLE_LIFETIME_MIN_MS 2000
#define PARTICLE_LIFETIME_MAX_MS 3000

struct color {
    u8 r, g, b;
};

#define BACKGROUND_COLOR ((struct color) { 0, 0, 0 })

struct screen {
    u8 *pixels;
    u32 width, height, pitch;
    u32 dot_size;
    u8 r_shift, g_shift, b_shift;
};

/*
 * A moving dot on the screen. Its position is in pixels and its velocity in
 * pixels per second, both stored as 48.16 fixed point numbers.
 */
struct body {
    i64 x, y;
    i64 vx, vy;
    struct color color;
};

struct particle_seed {
    i64 x, y;
    i64 speed;
};

static io_window s_fb_window;
static struct screen s_screen;
static i64 s_gravity;

static u64 s_rand_state;

static u32 s_runtime_seconds;
module_parameter(s_runtime_seconds);

static struct task *s_stats_task;

static u32 s_num_alive;
static u64 s_num_spawned;
static u64 s_num_failed;
static u64 s_worst_lateness_ns;

// SplitMix64 by Steele, Lea and Flood (OOPSLA 2014)
static u32 rand_u32(void)
{
    u64 z;

    z = atomic_fetch_add(&s_rand_state, 0x9E3779B97F4A7C15ull, MO_RELAXED);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;

    return (z ^ (z >> 31)) >> 32;
}

static u32 rand_below(u32 limit)
{
    return rand_u32() % limit;
}

static i64 rand_between(i64 first, i64 last)
{
    return first + rand_below(last - first + 1);
}

// A random bright color, with every channel at least half of its maximum
static struct color rand_color(void)
{
    struct color color;

    color.r = rand_between(UNSIGNED_HALF_MAX(u8), UNSIGNED_MAX(u8));
    color.g = rand_between(UNSIGNED_HALF_MAX(u8), UNSIGNED_MAX(u8));
    color.b = rand_between(UNSIGNED_HALF_MAX(u8), UNSIGNED_MAX(u8));

    return color;
}

// 'pct' percent of 'length' pixels, as a 48.16 fixed point number
static i64 percent_of(u32 length, u32 pct)
{
    return (i64)length * pct * FP_ONE / 100;
}

static u32 pixel_value(struct color color)
{
    struct screen *screen = &s_screen;
    u32 value;

    value = (u32)color.r << screen->r_shift;
    value |= (u32)color.g << screen->g_shift;
    value |= (u32)color.b << screen->b_shift;

    return value;
}

static u32 *screen_row(u32 y)
{
    return (u32*)(s_screen.pixels + y * s_screen.pitch);
}

static void plot(i64 x, i64 y, struct color color)
{
    struct screen *screen = &s_screen;
    u32 *row, value, dx, dy;

    if (x < 0 || y < 0)
        return;
    if (x + screen->dot_size > screen->width)
        return;
    if (y + screen->dot_size > screen->height)
        return;

    value = pixel_value(color);

    for (dy = 0; dy < screen->dot_size; dy++) {
        row = screen_row(y + dy);

        for (dx = 0; dx < screen->dot_size; dx++)
            row[x + dx] = value;
    }
}

static void note_lateness(u64 lateness_ns)
{
    u64 worst;

    worst = atomic_load_relaxed(&s_worst_lateness_ns);

    while (lateness_ns > worst) {
        if (atomic_cmpxchg_relaxed(&s_worst_lateness_ns, &worst, lateness_ns))
            break;
    }
}

static void sleep_for_a_frame(u32 ms)
{
    u64 before, slept_ns, wanted_ns;

    wanted_ns = ms * NS_PER_MS;
    before = ns_since_boot();
    sleep_ns(wanted_ns);
    slept_ns = ns_since_boot() - before;

    note_lateness(slept_ns - wanted_ns);
}

static void body_fly(struct body *body, u32 lifetime_ms)
{
    u32 frame_ms, elapsed_ms = 0, frame = 0;
    i64 x, y;

    while (elapsed_ms < lifetime_ms) {
        x = body->x / FP_ONE;
        y = body->y / FP_ONE;

        /*
         * Frame number 'frame' ends frame / FRAMES_PER_SEC seconds after the
         * first frame started, rounded down to a whole millisecond.
         */
        frame++;
        frame_ms = frame * MS_PER_SEC / FRAMES_PER_SEC - elapsed_ms;

        plot(x, y, body->color);
        sleep_for_a_frame(frame_ms);
        plot(x, y, BACKGROUND_COLOR);

        body->x += body->vx * frame_ms / SIGNED_MS_PER_SEC;
        body->y += body->vy * frame_ms / SIGNED_MS_PER_SEC;
        body->vy += s_gravity * frame_ms / SIGNED_MS_PER_SEC;

        elapsed_ms += frame_ms;
    }
}

static void particle_thread(void *user)
{
    struct particle_seed *seed = user;
    struct body body;
    i64 dx, dy;

    /*
     * Pick a random point inside the unit circle. Its direction and its
     * distance from the center become the direction and the speed of the
     * particle.
     */
    do {
        dx = rand_between(-FP_ONE, FP_ONE);
        dy = rand_between(-FP_ONE, FP_ONE);
    } while (dx * dx + dy * dy > FP_ONE * FP_ONE);

    body.x = seed->x;
    body.y = seed->y;
    body.vx = dx * seed->speed / FP_ONE;
    body.vy = dy * seed->speed / FP_ONE;
    body.color = rand_color();

    free(seed);

    body_fly(
        &body,
        rand_between(PARTICLE_LIFETIME_MIN_MS, PARTICLE_LIFETIME_MAX_MS)
    );
    atomic_fetch_sub(&s_num_alive, 1, MO_RELAXED);
}

static bool spawn(task_fn_t fn, void *user, const char *name)
{
    error_t ret;

    atomic_fetch_add(&s_num_alive, 1, MO_RELAXED);

    ret = kthread_run(fn, user, "%s", name);
    if (is_error(ret)) {
        atomic_fetch_sub(&s_num_alive, 1, MO_RELAXED);
        atomic_fetch_add(&s_num_failed, 1, MO_RELAXED);
        return false;
    }

    atomic_fetch_add(&s_num_spawned, 1, MO_RELAXED);
    return true;
}

static void explode(struct body *rocket)
{
    struct particle_seed *seed;
    u32 i, num_particles;
    i64 speed;

    num_particles = rand_between(
        PARTICLES_PER_ROCKET_MIN, PARTICLES_PER_ROCKET_MAX
    );
    speed = rand_between(
        percent_of(s_screen.height, PARTICLE_SPEED_MIN_PCT),
        percent_of(s_screen.height, PARTICLE_SPEED_MAX_PCT)
    );

    for (i = 0; i < num_particles; i++) {
        seed = alloc(sizeof(*seed), ALLOC_GENERIC);
        if (seed == nullptr) {
            atomic_fetch_add(&s_num_failed, 1, MO_RELAXED);
            continue;
        }

        seed->x = rocket->x;
        seed->y = rocket->y;
        seed->speed = speed;

        if (!spawn(particle_thread, seed, "particle"))
            free(seed);
    }
}

static void rocket_thread(void *user)
{
    struct screen *screen = &s_screen;
    struct body body;
    i64 max_vx;

    UNREFERENCED_PARAMETER(user);

    max_vx = percent_of(screen->width, ROCKET_SIDEWAYS_SPEED_MAX_PCT);

    body.x = (i64)screen->width / 2 * FP_ONE;
    body.y = ((i64)screen->height - 1) * FP_ONE;
    body.vx = rand_between(-max_vx, max_vx);
    body.vy = -rand_between(
        percent_of(screen->height, ROCKET_RISE_SPEED_MIN_PCT),
        percent_of(screen->height, ROCKET_RISE_SPEED_MAX_PCT)
    );
    body.color = rand_color();

    body_fly(
        &body, rand_between(ROCKET_LIFETIME_MIN_MS, ROCKET_LIFETIME_MAX_MS)
    );
    explode(&body);

    atomic_fetch_sub(&s_num_alive, 1, MO_RELAXED);
}

static u64 count_switches(void)
{
    u64 total = 0;
    cpu_t cpu;

    for_each_present_cpu(cpu)
        total += sched_nr_switches(cpu);

    return total;
}

static void stats_thread(void *user)
{
    u64 switches, prev_switches, worst_ns;

    UNREFERENCED_PARAMETER(user);

    prev_switches = count_switches();

    while (!kthread_should_stop()) {
        sleep_ns(STATS_PERIOD_MS * NS_PER_MS);

        switches = count_switches();
        worst_ns = atomic_xchg(&s_worst_lateness_ns, 0, MO_RELAXED);

        pr_info(
            "%u threads, %llu spawned, %llu failed, %llu switches/s, "
            "worst wakeup %llu us late\n",
            atomic_load_relaxed(&s_num_alive),
            atomic_load_relaxed(&s_num_spawned),
            atomic_load_relaxed(&s_num_failed),
            (switches - prev_switches) * MS_PER_SEC / STATS_PERIOD_MS,
            worst_ns / NS_PER_US
        );

        prev_switches = switches;
    }
}

static void launcher_thread(void *user)
{
    u32 i, num_rockets;
    u64 end_ns = 0, interval_ms;

    UNREFERENCED_PARAMETER(user);

    if (s_runtime_seconds != 0)
        end_ns = ns_from_now(s_runtime_seconds * NS_PER_SEC);

    for (;;) {
        num_rockets = rand_between(
            ROCKETS_PER_VOLLEY_MIN, ROCKETS_PER_VOLLEY_MAX
        );

        for (i = 0; i < num_rockets; i++)
            spawn(rocket_thread, nullptr, "rocket");

        interval_ms = rand_between(
            VOLLEY_INTERVAL_MIN_MS, VOLLEY_INTERVAL_MAX_MS
        );
        sleep_ns(interval_ms * NS_PER_MS);

        if (end_ns != 0 && ns_since_boot() >= end_ns)
            break;
    }

    while (atomic_load_relaxed(&s_num_alive) != 0)
        sleep_ns(DRAIN_POLL_PERIOD_MS * NS_PER_MS);

    kthread_stop(s_stats_task);

    pr_info(
        "done after %u s: %llu spawned, %llu failed\n", s_runtime_seconds,
        atomic_load_relaxed(&s_num_spawned),
        atomic_load_relaxed(&s_num_failed)
    );
}

static void INIT_CODE screen_clear(void)
{
    struct screen *screen = &s_screen;
    u32 *row, value, x, y;

    value = pixel_value(BACKGROUND_COLOR);

    for (y = 0; y < screen->height; y++) {
        row = screen_row(y);

        for (x = 0; x < screen->width; x++)
            row[x] = value;
    }
}

static error_t INIT_CODE screen_init(void)
{
    struct screen *screen = &s_screen;
    struct ultra_framebuffer *fb;
    error_t ret;

    if (!g_boot_ctx.fb) {
        pr_notice("no framebuffer available\n");
        return ENODEV;
    }

    fb = &g_boot_ctx.fb->fb;

    switch (fb->format) {
    case ULTRA_FB_FORMAT_RGBX8888:
        screen->r_shift = 24;
        screen->g_shift = 16;
        screen->b_shift = 8;
        break;
    case ULTRA_FB_FORMAT_XRGB8888:
        screen->r_shift = 16;
        screen->g_shift = 8;
        screen->b_shift = 0;
        break;
    default:
        pr_warn(
            "unsupported framebuffer format %u (%u bpp)\n",
            fb->format, fb->bpp
        );
        return ENOSYS;
    }

    ret = io_window_map_wc(
        fb->physical_address, fb->pitch * fb->height, &s_fb_window
    );
    if (is_error(ret))
        return ret;

    screen->pixels = io_window_raw_ptr(&s_fb_window);
    screen->width = fb->width;
    screen->height = fb->height;
    screen->pitch = fb->pitch;
    screen->dot_size = MAX(fb->height / DOTS_PER_SCREEN_HEIGHT, 1u);

    screen_clear();
    return EOK;
}

static error_t INIT_CODE fireworks_init(void)
{
    error_t ret;

    ret = screen_init();
    if (is_error(ret))
        return ret == ENODEV ? EOK : ret;

    s_gravity = percent_of(s_screen.height, GRAVITY_PCT);
    s_rand_state = ns_since_boot();

    s_stats_task = kthread_create(stats_thread, nullptr, "fireworks-stats");
    if (s_stats_task == nullptr)
        return ENOMEM;

    pr_info("%ux%u\n", s_screen.width, s_screen.height);

    ret = kthread_run(launcher_thread, nullptr, "fireworks");
    if (is_error(ret)) {
        kthread_stop(s_stats_task);
        return ret;
    }

    kthread_start(s_stats_task);
    return EOK;
}
MODULE_INIT(fireworks_init);
