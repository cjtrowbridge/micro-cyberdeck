/*
 * hat-sound v3 — 1-bit DAC on PIN 12, with a discriminator mode.
 *
 * History: v1 (nanosleep ticks) = crackle. v2 (spin-wait ticks) = still
 * crackle. The clock fix may have been real but was not the whole story.
 *
 * v3.1/v3.2 (post-run fixups): the sigma-delta step was unit-wrong (2*FS
 * step against +-0.5FS tones -> 0% duty -> dead pin; fixed to +-FS step,
 * GAIN 0.85; offline sim: 50% mean duty, +-42.5% swing). Then a hybrid
 * sleep+spin deadline wait overslept ~4.3 us per tick (measured dev_avg)
 * -> modem-like warble; back to pure spin. v3.2: SCHED_FIFO prio 1 -> 50
 * (only IRQs preempt us now), -c CORE pinning selector, -o PATH stats
 * sink, and >1ms/>10ms stall counters in the stats line — hunting the
 * 50 ms stalls.
 * Root cause of the stalls (ftrace + stats, 2026-09-15): NOT competing
 * threads — the box sat ~idle during every 1 Hz x ~50 ms gap. It was
 * the kernel RT bandwidth limit: kernel.sched_rt_runtime_us (default
 * 950000) caps RT tasks at 950 ms per 1000000 us period, so a
 * 100%-one-core FIFO task froze ~50 ms per second at any priority.
 * Fix: sysctl kernel.sched_rt_runtime_us=-1 (persisted by setup.sh).
 * Two suspects left:
 *   (a) signal level — v2 tones were -12 dBFS; the amp input filter's
 *       1-bit hiss floor can sit ABOVE a quiet tone;
 *   (b) modulation — the amp's input RC may not tolerate sigma-delta
 *       high-frequency shaping.
 * v3 answers both:
 *   - ticks back to 192 kHz, OS=4 (noise pushed higher, hiss quieter)
 *   - default test tones at -6 dB (4x the power of v2)
 *   - mode -p : PLAIN 1 kHz square wave through the SAME tick clock,
 *     NO sigma-delta, NO dither — if -p is clean and sigma-delta is
 *     crackly, the amp hates the modulation; if -p also crackles,
 *     the pin/level/clock is the problem
 *   - mode -t <Hz> [-d <sec>] : one tone for N seconds (hand-tuning)
 *   - per-segment timing stats mirrored to /run/hat-sound-stats.v3
 *     (root-writable; agent-readable) so no more manual pasting
 *
 * Clock: 192 kHz ticks = 5208 ns each, pure spin-wait on CLOCK_MONOTONIC
 * deadlines (never recompute from now; next += tick), mlockall +
 * SCHED_FIFO + core-3 pinning, 4-tick overrun re-anchor (no clumps).
 *
 * v3.3 (2026-09-15): real audio. -F FILE (or -F -= stdin): raw s16le
 * 48 kHz mono played through the SAME 192 kHz sigma-delta tick clock.
 * A feeder thread (the parent, in -F mode) fills a sample ring; the RT
 * tick thread consumes one sample every OS ticks — pin tick rate never
 * drops. Pin parks LOW at EOF or ctrl-c.
 *
 * v3.4 (2026-09-15): the 4x-pacing fix. The 4-tick overrun re-anchor at
 * the bottom of the loop compared `now - next` UNSIGNED; on a healthy
 * clock now < next, so the difference underflows to ~2^64 and the test
 * is true EVERY tick. `tis` was reset to 0 on every tick: mode 2 then
 * consumed a ring sample EVERY tick (4x real-time; self-reported 3.98x)
 * and mode 0 advanced the sine phase every tick (all sigma tones ~4x
 * pitch — the unexplained "modem" of early v3 runs). A dedicated bench
 * that links the real engine looked correct because it never ran main()
 * — the engine always paced at 1 sample / 4 ticks; only main()'s loop
 * held the broken re-anchor. The compare is now signed
 * ((int64_t)(now - next) > 4 * TICK_NS): a true condition now means
 * behind by more than 4 real ticks. -p mode was immune all along
 * (uses elapsed time only), which is why it always sounded normal.
 *
 * v3.5 (2026-09-15): -i IDLE mode, for the service. Acquires the amp
 * pin (root), parks it LOW, and holds until SIGTERM/SIGINT — no tick
 * clock, no CPU, no stats rewrite. That is what gamepi-sound.service
 * runs at boot (silent: pin owned and LOW, no surprise sound).
 * Playing = stop the service (pin released LOW), run
 * `hat-sound -F <file>` (the RT parent feeds the ring inline — no
 * feeder thread), restart the service (docs/setup.md "Sound").
 *   ffmpeg -i in.mp3 -ac 1 -ar 48000 -f s16le - | sudo hat-sound
 *   sudo hat-sound -F /path/to/mono48k.s16
 *
 * Build: gcc -O2 -Wall -Wextra -o hat-sound tools/hat-sound.c -lm -lpthread
 * Run : sudo /home/cj/hat-sound            (25 s sigma-delta program)
 *       sudo /home/cj/hat-sound -p         (10 s plain 1 kHz square)
 *       sudo /home/cj/hat-sound -t 440 -d 5
 *       ffmpeg ... -f s16le - | sudo /home/cj/hat-sound  (real audio)
 *       sudo /home/cj/hat-sound -i         (idle: pin LOW, holds)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>

/* ---- kernel GPIO v2 ABI (linux v6.6 uapi, copied verbatim — board     */
/* runs 6.6.98-vendor-sun60iw2; layout proven on this board: v1/v2 both  */
/* acquired line 37 and toggled it).  NEVER include                      */
/* /usr/include/linux/gpio.h: that is the host distro's kernel tree, not */
/* necessarily the running kernel's.                                     */
/* CRITICAL: gpio_v2_line_config DOES carry attrs[10] in v6.6. The v2    */
/* ioctl encodes the struct's size in the command word, so any           */
/* truncation => EINVAL from the kernel. Keep byte-identical.            */

#define GPIO_MAX_NAME_SIZE          32
#define GPIO_V2_LINES_MAX           64
#define GPIO_V2_LINE_NUM_ATTRS_MAX  10

enum gpio_v2_line_flag {
    GPIO_V2_LINE_FLAG_USED                 = (1ULL << 0),
    GPIO_V2_LINE_FLAG_ACTIVE_LOW           = (1ULL << 1),
    GPIO_V2_LINE_FLAG_INPUT                = (1ULL << 2),
    GPIO_V2_LINE_FLAG_OUTPUT               = (1ULL << 3),
    GPIO_V2_LINE_FLAG_EDGE_RISING          = (1ULL << 4),
    GPIO_V2_LINE_FLAG_EDGE_FALLING         = (1ULL << 5),
    GPIO_V2_LINE_FLAG_OPEN_DRAIN           = (1ULL << 6),
    GPIO_V2_LINE_FLAG_OPEN_SOURCE          = (1ULL << 7),
    GPIO_V2_LINE_FLAG_BIAS_PULL_UP         = (1ULL << 8),
    GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN       = (1ULL << 9),
    GPIO_V2_LINE_FLAG_BIAS_DISABLED        = (1ULL << 10),
    GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME = (1ULL << 11),
};

struct gpio_v2_line_values {
    uint64_t bits;
    uint64_t mask;
};

struct gpio_v2_line_attribute {
    uint32_t id;
    uint32_t padding;
    union {
        uint64_t flags;
        uint64_t values;
        uint32_t debounce_period_us;
    };
};

struct gpio_v2_line_config_attribute {
    struct gpio_v2_line_attribute attr;
    uint64_t mask;
};

struct gpio_v2_line_config {
    uint64_t flags;
    uint32_t num_attrs;
    uint32_t padding[5];
    struct   gpio_v2_line_config_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};

struct gpio_v2_line_request {
    uint32_t offsets[GPIO_V2_LINES_MAX];
    char     consumer[GPIO_MAX_NAME_SIZE];
    struct   gpio_v2_line_config config;
    uint32_t num_lines;
    uint32_t event_buffer_size;
    uint32_t padding[5];
    int32_t  fd;
};

#define GPIO_CHIP_IOCTL_BASE            0xB4
#define GPIO_V2_GET_LINE_IOCTL          _IOWR(GPIO_CHIP_IOCTL_BASE, 0x07, struct gpio_v2_line_request)
#define GPIO_V2_LINE_SET_VALUES_IOCTL   _IOWR(GPIO_CHIP_IOCTL_BASE, 0x0F, struct gpio_v2_line_values)

/* Byte-size pins from the upstream v6.6 layout:
 * attr = 4+4+8 = 16, config_attr = 16+8 = 24,
 * config = 8+4+20+10*24 = 272, request = 64*4+32+272+4+4+20+4 = 592.
 * The ioctl word encodes these sizes, so drift here = EINVAL on the board. */
_Static_assert(sizeof(struct gpio_v2_line_values) == 16, "values size");
_Static_assert(sizeof(struct gpio_v2_line_config) == 272, "config size");
_Static_assert(sizeof(struct gpio_v2_line_request) == 592, "request size");

/* ---- pin ------------------------------------------------------------- */

#define CHIP_PATH "/dev/gpiochip0"
#define AMP_LINE  37u                /* PB5 = S3W header pin 12 */

/* ---- audio grid ------------------------------------------------------ */

#define TICK_HZ  192000.0
#define OS       4u                  /* pin ticks per audio sample  */
#define AUD_HZ   (TICK_HZ / OS)      /* = 48000 Hz                  */
#define SD_FS    8192                /* sigma-delta full scale      */
#define GAIN     0.85                /* duty band 50% +- 42.5% -> 7.5%..92.5% */

#define TICK_NS  (uint64_t)(1000000000.0 / TICK_HZ)   /* 5208 */
#define PHASE_S  5.0

static const char *STATS_PATH = "/run/hat-sound-stats.v3";

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

static int      g_core    = 1;                           /* -c CORE    */
static int      g_rtprio  = 98;                          /* -r PRIO    */
static char     alt_stats[256] = { 0 };                  /* -o PATH    */

static int g_line_fd = -1;

/* ---- real-audio input (v3.3): raw s16le mono @ 48 kHz ---------------- */
/* Byte-stream ring: prod/cons are byte indices (monotonic, wrapped by a
 * mask on use). Two trailing bytes stay unused, so a full ring and an
 * empty ring stay distinguishable, and the consumer never straddles the
 * wrap without the (p+1)&mask dance below. */

#define RING_MAX 8192u                       /* bytes = 4096 s16 samples */

static uint8_t *g_ring  = NULL;
static const unsigned long g_mask = RING_MAX - 1;
static _Atomic unsigned long g_prod, g_cons;   /* unbounded wrap counters */
static _Atomic int   g_feed_eof  = 0;
static _Atomic uint64_t g_underrun;            /* sample slots seen empty */
static int   g_src_fd    = -1;                /* 0 = stdin; else file */
static char  g_src_name[256] = { 0 };

static inline unsigned long ring_free(void)
{
    long used = (long)((long)g_prod - (long)g_cons);
    long free_n = (long)(RING_MAX - 2) - used;
    return free_n > 0 ? (unsigned long)free_n : 0ul;
}

/* Feed g_src into the ring. 0 = got data, -1 = EOF/close, -10 = full. */
static int feed_more(void)
{
    for (;;) {
        unsigned long free_n = ring_free();
        if (free_n == 0) return -10;
        unsigned long at   = g_prod & g_mask;
        unsigned long tail = RING_MAX - at;      /* never straddle the buffer end */
        if (free_n > tail) free_n = tail;
        ssize_t r = read(g_src_fd, g_ring + at, free_n);
        if (r > 0)      { g_prod += (unsigned long)r; return 0; }
        if (r < 0 && errno == EINTR)  continue;
        if (r < 0 && errno == EAGAIN) { usleep(2000); continue; }
        if (r < 0)
            fprintf(stderr, "hat-sound: read source: %s\n", strerror(errno));
        else
            fprintf(stderr, "hat-sound: EOF after %lu source bytes\n",
                    (unsigned long)g_prod);
        return -1;                           /* 0 = clean EOF */
    }
}

static void *feed_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int r = feed_more();
        if (r == 0)      continue;           /* got bytes */
        if (r == -10) { usleep(10000); continue; }   /* ring full: wait */
        break;                              /* -1: true EOF */
    }
    g_feed_eof = 1;                          /* engine ends on empty ring */
    return NULL;
}

static int sample_next(int16_t *s16)
{
    /* two whole bytes must be available: read() may deliver odd counts, so a
     * trailing single byte waits for its partner (<= 20 us, inaudible). */
    if (g_cons + 2 <= g_prod) {
        unsigned long p = g_cons & g_mask;
        unsigned hi = g_ring[(p + 1) & g_mask];
        *s16 = (int16_t)(g_ring[p] | (hi << 8));
        g_cons += 2;
        return 1;
    }
    g_underrun++;
    return 0;                                /* hold last sample */
}

static int fd_head_check(void)
{
    uint8_t h[16] = { 0 };
    size_t  got = 0;
    while (got < sizeof(h)) {
        ssize_t r = read(g_src_fd, h + got, sizeof(h) - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (got >= 3 && memcmp(h, "ID3", 3) == 0)
        fprintf(stderr, "warning: input looks like an MP3 — hat-sound wants RAW s16le 48 kHz mono.\n"
                "  re-encode: ffmpeg -i %s -ac 1 -ar 48000 -f s16le - | sudo hat-sound\n",
                g_src_name);
    else if (got >= 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0)
        fprintf(stderr, "warning: input looks like a WAV — hat-sound wants RAW s16le 48 kHz mono.\n"
                "  re-encode: ffmpeg -i %s -ac 1 -ar 48000 -f s16le - | sudo hat-sound\n",
                g_src_name);
    (void)lseek(g_src_fd, 0, SEEK_SET);
    return 0;
}

static inline void pin_set(int level)
{
    struct gpio_v2_line_values v;
    v.bits = (uint64_t)(level & 1);
    v.mask = 1ULL;
    (void)ioctl(g_line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

static int pin_acquire(void)
{
    int chip_fd = open(CHIP_PATH, O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        fprintf(stderr, "hat-sound: open %s: %s\n", CHIP_PATH, strerror(errno));
        return -1;
    }
    struct gpio_v2_line_request r = {0};
    r.offsets[0]   = (uint32_t)AMP_LINE;
    r.num_lines    = 1;
    r.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(r.consumer, sizeof(r.consumer), "hat-sound");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &r) < 0) {
        fprintf(stderr, "hat-sound: GET_LINE [%u]: %s", AMP_LINE, strerror(errno));
        int e = errno;
        if (e == EBUSY)
            fprintf(stderr, "  line in use — check: gpioget gpiochip0 %u\n", AMP_LINE);
        else
            fprintf(stderr, "\n");
        close(chip_fd);
        return -1;
    }
    g_line_fd = r.fd;

    struct gpio_v2_line_values v = { 0, 1ULL };        /* park LOW first */
    if (ioctl(g_line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0) {
        fprintf(stderr, "hat-sound: first SET_VALUES: %s\n", strerror(errno));
        close(g_line_fd);
        g_line_fd = -1;
        return -1;
    }
    return 0;
}

static void pin_release(void)
{
    if (g_line_fd > 0) {
        pin_set(0);
        close(g_line_fd);
        g_line_fd = -1;
    }
}

/* ---- realtime setup --------------------------------------------------- */

static void rt_setup(void)
{
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0)
        fprintf(stderr, "rt: mlockall OK\n");
    else
        fprintf(stderr, "rt: mlockall: %s (continuing)\n", strerror(errno));

    (void)prctl(PR_SET_TIMERSLACK, 1000);              /* 1 us slack */

    /* v3.2p: prio 98 (was 1, then 50). The ~50 ms/1 s stalls were NOT
     * priority contention — the ftrace capture showed the whole box
     * idle inside every gap. True root cause: the kernel RT bandwidth
     * limit (kernel.sched_rt_runtime_us default 950000 per 1000000 us
     * period) throttled our FIFO task for the last ~50 ms of each
     * period, identically at prio 1/50/98. Fix: sysctl -w
     * kernel.sched_rt_runtime_us=-1 (persisted by setup.sh); the
     * priority here is belt-and-braces only. */
    struct sched_param p = { .sched_priority = g_rtprio };
    if (sched_setscheduler(0, SCHED_FIFO, &p) == 0)
        fprintf(stderr, "rt: SCHED_FIFO prio %d OK\n", g_rtprio);
    else
        fprintf(stderr, "rt: SCHED_FIFO: %s (continuing)\n", strerror(errno));

    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(g_core, &cs);
    if (sched_setaffinity(0, sizeof(cs), &cs) == 0)
        fprintf(stderr, "rt: pinned to CPU %d\n", g_core);
    else
        fprintf(stderr, "rt: affinity: %s (continuing)\n", strerror(errno));
}

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ---- 1st-order 1-bit sigma-delta ------------------------------------- */

static int64_t g_sda = 0;

/* Classic 1st-order 1-bit sigma-delta. The quantizer step is +-SD_FS,
 * i.e. +-1 in FS units, and x is in the same units ([-FS, +FS]):
 *   g_sda += x - (out ? +SD_FS : -SD_FS)
 * A zero-mean tone x(t) = A*FS*sin() (A in [-1,1] of FS) holds the
 * integral near 0, so the emitted duty over one audio period is
 * 50% +/- A/2 — the amp's input RC integrates that into the analog
 * level. (The earlier o*(2*SD_FS) form stepped by +/-2 FS while a
 * GAIN-0.5 sine only swung to 0.5 FS => the integrator sat nearly
 * all negative => ~0% ones. v3 stats proved it: duty=0% for 25 s.)
 */
static inline int sd_bit(int16_t x)                    /* x in [-FS, +FS] */
{
    int o = (g_sda > 0);
    g_sda += (int64_t)x - (int64_t)(o ? SD_FS : -SD_FS);
    return o;
}

/* Pure spin to the deadline. Measured on this board: the hybrid sleep
 * overslept dev_avg ~= 4.3 us per tick at a 5.2 us period (audibly: a
 * modem); a pure spin measured ~= 0.3 us. A 192 kHz period leaves no
 * margin for any real sleep. Note: the 50 ms dev_max spikes are
 * preemption of our thread (IRQ/softirq), not our own spinning — each
 * tick is 5 us, we can't spin that long ourselves. */
static inline void wait_deadline(uint64_t next, uint64_t *now)
{
    while ((uint64_t)(*now = now_ns()) < next) {
    }
}

/* ---- test program ------------------------------------------------------ */

struct phase { double f0, f1; const char *label; };

static struct phase PHASES[] = {
    { 1000.0, 1000.0, "1000 Hz" },
    {  440.0,  440.0, "440 Hz"  },
    {  100.0,  100.0, "100 Hz"  },
    { 2000.0, 2000.0, "2000 Hz" },
    {   60.0, 8000.0, "ramp 60->8k Hz" },
};
#define N_PHASES (int)(sizeof(PHASES) / sizeof(PHASES[0]))

static char single_label[32];                    /* label for -t mode */

/* ---- engine ----------------------------------------------------------- */

/* One tick of work. mode 0 = sigma-delta tones; mode 1 = plain square of
 * plain_freq at 50% duty. seg = active phase index (passed in; the caller
 * owns segmenting so -t mode never leaks into later program phases).
 * Returns the pin level emitted. */
static int engine_tick(int mode, uint64_t el_ns, int seg, double plain_freq,
                       double *ph, int16_t *s16, int *tis)
{
    if (mode == 1) {
        /* plain 2*(f/TICK) toggle table: level = floor(2*f*t) mod 2 */
        double t = (double)el_ns * 1e-9;
        double v = 2.0 * plain_freq * t;
        return (int)((int64_t)(v + 0.5)) & 1;
    }

    if (mode == 2) {
        /* real PCM: one ring sample per OS-tick group, else hold last.
         * s16 full scale (32768) maps onto SD_FS (8192) i.e. /4, so a
         * loud file hits ~100% duty rather than overflowing the
         * quantizer into hard clipping. */
        if (*tis == 0)
            (void)sample_next(s16);
        *tis = (*tis + 1u) & (OS - 1u);
        return sd_bit((int16_t)(((int32_t)*s16 * (int32_t)SD_FS) >> 15));
    }

    long seg_ns = (long)(PHASE_S * 1e9);
    if (*tis == 0) {
        double t = (double)(el_ns % (uint64_t)seg_ns) / 1e9;
        const struct phase *P = &PHASES[seg];
        double f = P->f0 + (P->f1 - P->f0) * (t / PHASE_S);
        *ph += f / AUD_HZ;
        if (*ph >= 1.0) *ph -= (double)(uint32_t)*ph;
        if (*ph <  0.0) *ph += 1.0;
        *s16 = (int16_t)(GAIN * SD_FS * sin(2.0 * M_PI * *ph));
    }
    *tis = (*tis + 1u) & (OS - 1u);
    return sd_bit(*s16);
}

/* ---- stats --------------------------------------------------------------- */

struct seg { uint64_t n, late, high, sumd, maxd, x1ms, x10ms; };

static void seg_reset(struct seg *s)
{
    s->n = s->late = s->high = s->sumd = s->maxd = s->x1ms = s->x10ms = 0;
}

static FILE *g_stats_f = NULL;

static void stats_line(const char *label, const struct seg *s, uint64_t elapsed_ns)
{
    FILE *out = g_stats_f ? g_stats_f : stderr;
    double avg = s->n ? (double)s->sumd / (double)s->n : 0.0;
    unsigned long long pct = s->n ? s->high * 100 / s->n : 0;
    fprintf(out,
            "  %-14s t=%5.1fs ticks=%-7llu dev_avg=%8.1f ns dev_max=%6lld ns late1us=%-6llu x1ms=%-5llu x10ms=%-4llu und=%-5llu duty=%llu%%\n",
            label, (double)elapsed_ns / 1e9,
            (unsigned long long)s->n, avg, (long long)s->maxd,
            (unsigned long long)s->late, (unsigned long long)s->x1ms,
            (unsigned long long)s->x10ms, (unsigned long long)g_underrun,
            (unsigned long long)pct);
    if (g_stats_f) fflush(g_stats_f);
}

int main(int argc, char **argv)
{
    int    mode      = 0;               /* 0 sd-program, 1 plain, 2 real audio */
    double plain_hz  = 1000.0;
    double dur_s     = 10.0;            /* plain / single-tone duration */
    int    single    = 0;               /* -t: single tone, not the program */
    int    raw_src   = 0;               /* -F FILE|-: real s16le 48k mono    */
    int    idle      = 0;               /* -i: hold the pin LOW (service)    */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) { mode = 1; dur_s = 10.0; }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            mode = 0; single = 1; plain_hz = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dur_s = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            g_core = atoi(argv[++i]);
            if (g_core < 0 || g_core > 15) g_core = 1;
        }
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            g_rtprio = atoi(argv[++i]);
            if (g_rtprio < 1 || g_rtprio > 99) g_rtprio = 98;
        }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            snprintf(alt_stats, sizeof(alt_stats), "%s", argv[++i]);
        }
        else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            const char *src = argv[++i];
            raw_src = 1;
            if (strcmp(src, "-") != 0)
                snprintf(g_src_name, sizeof(g_src_name), "%s", src);
        }
        else if (strcmp(argv[i], "-i") == 0) {
            idle = 1;
        }
        else {
            fprintf(stderr,
                    "usage: hat-sound [-p] | [-t HZ] [-d SEC] | "
                    "[-F FILE|-] | [-i] [-c CORE] [-r PRIO] [-o PATH]\n");
            return 2;
        }
    }

    if (idle && (raw_src || single || mode == 1)) {
        fprintf(stderr, "hat-sound: -i is idle mode; takes no source or tone args\n");
        return 2;
    }
    if (raw_src && (mode == 1 || single)) {
        fprintf(stderr, "hat-sound: -F is standalone real-audio mode (drop -p/-t)\n");
        return 2;
    }
    if (raw_src) mode = 2;

    if (single) {
        /* one tone, N seconds — first phase becomes that tone */
        snprintf(single_label, sizeof(single_label), "%g Hz", plain_hz);
        PHASES[0].label = single_label;
        PHASES[0].f0 = PHASES[0].f1 = plain_hz;
    }
    if (raw_src)
        PHASES[0].label = "audio";

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    if (raw_src) {
        g_ring = malloc(RING_MAX);
        if (!g_ring) {
            fprintf(stderr, "hat-sound: ring alloc: %s\n", strerror(errno));
            return 1;
        }
        if (g_src_name[0]) {
            g_src_fd = open(g_src_name, O_RDONLY | O_CLOEXEC);
            if (g_src_fd < 0) {
                fprintf(stderr, "hat-sound: open %s: %s\n",
                        g_src_name, strerror(errno));
                return 1;
            }
            (void)fd_head_check();
        } else {
            g_src_fd = 0;
            if (isatty(0))
                fprintf(stderr, "note: stdin is a terminal — pipe PCM in "
                        "(ffmpeg ... -f s16le -) or use -F FILE\n");
        }
    }

    if (pin_acquire() < 0)
        return 1;

    if (idle) {
        /* Service mode (v3.5): hold the pin LOW and consume nothing.
         * pause() blocks until on_sig() sets g_stop (SIGTERM from the
         * unit, SIGINT from ctrl-c). No tick clock, no stats rewrite —
         * this process must stay off the RT budget. The exit path
         * (pin_release) parks the pin LOW. */
        fprintf(stderr,
                "hat-sound v3.5: line %u — idle: pin LOW until stopped "
                "(to play: systemctl stop gamepi-sound, then hat-sound -F <file>)\n",
                AMP_LINE);
        while (!g_stop)
            pause();
        pin_release();
        fprintf(stderr, "hat-sound: idle stopped — pin released LOW\n");
        return 0;
    }

    pthread_t feed_tid = 0;
    if (raw_src && g_src_fd == 0) {
        cpu_set_t fcs;
        CPU_ZERO(&fcs);
        CPU_SET(g_core, &fcs);
        if (pthread_create(&feed_tid, NULL, feed_thread, NULL) != 0) {
            fprintf(stderr, "hat-sound: feeder thread: %s\n", strerror(errno));
            pin_release();
            return 1;
        }
        /* created before rt_setup() => inherits plain CFS. It must NOT
         * share g_core: the tick thread spins (never sleeps) at FIFO 98,
         * which occupies the core 100% preemption-wise, so a CFS reader
         * there would starve. One neighbor core, parked in read() when
         * the ring is full, is cheap. */
        long nc = sysconf(_SC_NPROCESSORS_ONLN);
        if (nc < 2) nc = 2;
        CPU_ZERO(&fcs);
        CPU_SET((g_core % (int)nc + 1) % (int)nc, &fcs);
        (void)pthread_setaffinity_np(feed_tid, sizeof(fcs), &fcs);
        (void)pthread_setname_np(feed_tid, "hatfeed");
    }

    rt_setup();

    const char *stats_path = alt_stats[0] ? alt_stats : STATS_PATH;
    g_stats_f = fopen(stats_path, "w");
    if (g_stats_f) {
        fprintf(g_stats_f, "hat-sound v3.5 core=%d prio=%d @%d kHz OS=%u\n",
                g_core, g_rtprio, (int)(TICK_HZ / 1000), (unsigned)OS);
        if (mode == 2)
            fprintf(g_stats_f, "mode=real audio s16le 48 kHz mono (in: %s)\n",
                    g_src_name[0] ? g_src_name : "stdin");
        else if (single)
            fprintf(g_stats_f, "mode=single tone %.g Hz\n", plain_hz);
        else if (mode == 1)
            fprintf(g_stats_f, "mode=plain PWM %.g Hz\n", plain_hz);
        else
            fprintf(g_stats_f, "mode=sigma-delta program\n");
        fflush(g_stats_f);
    }

    int nph = 1;                        /* single/tone/real-audio: 1 phase */
    if (!single && mode == 0) nph = N_PHASES;

    if (mode == 2)
        fprintf(stderr,
                "hat-sound v3.5: line %u @ %.0f kHz OS=%u core=%d prio=%d "
                "mode=real-audio (s16le 48 kHz mono) — ctrl-c stops\n",
                AMP_LINE, TICK_HZ / 1000, (unsigned)OS, g_core, g_rtprio);
    else
        fprintf(stderr,
                "hat-sound v3.5: line %u @ %.0f kHz OS=%u core=%d prio=%d "
                "mode=%s dur=%.0fs — ctrl-c stops\n",
                AMP_LINE, TICK_HZ / 1000, (unsigned)OS, g_core, g_rtprio,
                mode == 1 ? "plain" : (single ? "single-tone" : "sigma-delta"),
                single ? dur_s : nph * PHASE_S);

    const long seg_ns   = (long)(PHASE_S * 1e9);
    const uint64_t total_ns =
        (mode == 2) ? UINT64_MAX
                    : (uint64_t)((single ? dur_s : (double)nph * PHASE_S) * 1e9);

    double  ph  = 0.0;
    int16_t s16 = 0;
    int     tis = 0;

    uint64_t t0   = now_ns();
    uint64_t next = t0 + TICK_NS;
    int      seg_i = 0;
    struct seg s;
    seg_reset(&s);

    while (!g_stop) {
        uint64_t now  = now_ns();
        uint64_t el   = now - t0;

        int seg_now  = single ? 0 : (int)(el / (uint64_t)seg_ns);
        if (seg_now > nph - 1) seg_now = nph - 1;
        if (seg_now != seg_i) {
            stats_line(PHASES[seg_i].label, &s, el);
            seg_reset(&s);
            seg_i = seg_now;
        }

        if (mode == 2 && g_feed_eof && g_cons + 2 > g_prod)
            break;                        /* EOF: nothing left to read */

        int lvl = engine_tick(mode, el, seg_i, plain_hz, &ph, &s16, &tis);
        pin_set(lvl);
        if (lvl) s.high++;

        if (mode == 2 && g_src_fd > 0 && !g_feed_eof &&
            ring_free() >= RING_MAX / 4) {
            if (feed_more() == -1)
                g_feed_eof = 1;
        }

        wait_deadline(next, &now);
        int64_t dev = (int64_t)(now - next);
        if (dev < 0) dev = -dev;
        uint64_t du = (uint64_t)dev;                  /* already abs() */
        s.sumd += du;
        if (du > s.maxd) s.maxd = du;
        if (dev > 1000) s.late++;
        if (du > 1000000ull)  s.x1ms++;
        if (du > 10000000ull) s.x10ms++;
        s.n++;

        next += TICK_NS;

        /* Signed compare, on purpose: on a healthy clock now < next (the
         * deadline is still ahead) and `now - next` then UNDERFLOWS the
         * uint64_t, making an unsigned compare true EVERY tick. That reset
         * `tis` to 0 on every tick, so mode 0 advanced the phase and mode 2
         * consumed a sample on EVERY tick = 4x pitch / 4x rate. Found the
         * hard way (v3.3 field runs: 3.98x real-time, self-reported). */
        if ((int64_t)(now - next) > (int64_t)(4 * TICK_NS)) {
            next = now + TICK_NS;
            tis = 0;
        }

        if (el >= total_ns)
            break;
    }

    stats_line(PHASES[seg_i].label, &s, now_ns() - t0);
    if (mode == 2) {
        /* Self-report: the whole mystery so far has been a stats file read
         * that disagreed with what actually played. This line cannot be
         * stale — it is printed at exit: samples consumed, the effective
         * playback rate, and the multiple of real-time (1.00x = correct). */
        uint64_t el_end  = now_ns() - t0;
        unsigned long cb = (unsigned long)g_cons;
        double rt_s  = el_end ? (double)el_end / 1e9 : 0.0;
        double pps   = rt_s ? (double)(cb / 2) / rt_s : 0.0;
        fprintf(stderr,
                "hat-sound: %s — source %lu B, consumed %lu B (%lu samples, %.1f Hz, %.2fx real-time), underruns %lu\n",
                g_stop ? "stopped (signal)" : "EOF, ring drained",
                (unsigned long)g_prod, cb, cb / 2, pps, pps / AUD_HZ,
                (unsigned long)g_underrun);
    }
    if (g_stats_f) {
        fprintf(g_stats_f, "done\n");
        fclose(g_stats_f);
        g_stats_f = NULL;
    }
    pin_release();
    fprintf(stderr, "hat-sound: done — pin released LOW, stats at %s\n", stats_path);
    return 0;
}