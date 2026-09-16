/* hat_pace_selftest — offline pacing self-test for the pcm_hat plugin.
 *
 * Plan step (plans/current/2026-09-15-13-17-28_hat-alsa-device-and-tts.md,
 * WS3 revision 3.1r.2): prove the pacing contract in pure C arithmetic,
 * timers, and socket pairs BEFORE any live board test. Nothing plays on
 * the board until this is green.
 *
 * The bug under test (build-3 incident, journal
 * 2026-09-16-hat-plugin-builds-audit-and-redesign.md): v1's pointer is
 * anchored to BYTES SENT, not bytes CONSUMED (hat_state's mass/drain:
 * `mass` = 48 kHz samples handed to the socket, `drain` = wall time at
 * which `mass` will be consumed — valid only while the engine is
 * continuously fed). Two incident classes, both reproduced here:
 *
 *   RED (T1): healthy engine + 2.000 s file. The moment the last byte is
 *             sent, the bookkeeping declares the WHOLE file consumed;
 *             drain() returns at the last-send anchor while queue+ring
 *             still hold ~0.4 s of audio; close() = EOF truncates the
 *             tail of EVERY file. No error, no backwards pointer — the
 *             under-report is silent by construction.
 *   RED (T6a): the engine's consumption lags the send queue for 600 ms
 *             (the live incident class). v1 has no path that turns
 *             "engine not consuming" into an error: the blocking send
 *             soaks the stall, the run silently stretches, and the same
 *             shape × RT jitter × a larger queue is the 9-minute spin.
 *   GREEN (T2/T3/T4/T5/T6b): the v2 gated model — pointer = where the
 *             engine's consumption front actually is (each accepted chunk
 *             chews at the wire clock from its RING-ENTRY time, back to
 *             back), nonblocking transport bounded by a stall watchdog,
 *             stop-closes for a fresh job per start, a real drain —
 *             keeps the full pattern in healthy runs, surfaces a stalled
 *             or dead engine as XRUN within the watchdog bound, and
 *             recovers via lazy reconnect.
 *
 * Fidelity notes
 *   - The mini-core replicates the pinned alsa-lib 1.2.14 semantics:
 *     snd_pcm_write_areas (PREPARED: direct transfer, auto-start at
 *     start_threshold; RUNNING: hwsync, avail_update, size > avail &&
 *     may_wait(avail < avail_min) -> -EAGAIN in nonblock mode; xfer =
 *     min(size, avail); true avail MAY BE NEGATIVE when the send queue
 *     exceeds the buffer), snd_pcm_ioplug_hwsync/hw_ptr_update (pointer
 *     >= 0: hw advances by the delta against the LAST PLUGIN POINTER,
 *     wrap = buffer_size + p - last, no BOUNDARY_WA in this instance;
 *     pointer < 0: XRUN, or drop while DRAINING; prepare resets last_hw
 *     via reset), drain (poll loop), aplay's nonblock client shape
 *     (writei; -EPIPE -> snd_pcm_prepare() -> retry same data;
 *     -EAGAIN -> snd_pcm_wait(poll) -> retry).
 *   - The fake engine mirrors the v3.6 contract: AF_UNIX SOCK_STREAM,
 *     one connection = one job. A FEEDER thread pulls recv() into a
 *     fixed ring continuously (like the daemon's feed_thread; ring full
 *     -> sleep, never drops), a TICK clock consumes one 48 kHz sample
 *     per tick from the ring (tick rate = the wire sample rate: the
 *     real engine's 192 kHz spin at OS=4 is 1 ring sample every 4 ticks
 *     = 48 kHz; empty tick = 1 underrun tick, like the daemon's
 *     hold-last-sample). Each byte's RING-ENTRY time is
 *     timestamped into a delivery ladder; the v2 model chews accepted
 *     chunks from those entries, so model consumption lags true
 *     consumption by at most one ring — and the v2 drain margin waits
 *     out that last ring before EOF.
 *   - Kernel socket buffers are the real transport budget (v1 64 KiB
 *     sndbuf (advisory, doubling applies) + 32 KiB rcvbuf + 8 KiB ring
 *     ~= 400 ms of wire audio; v2 16 KiB sndbuf -> ~160 ms). Backlog
 *     pressure therefore shows up as negative true avail during the
 *     initial fill — the assertions account for that.
 *
 * Build and run (on the board or any aarch64/x86 host):
 *   cc -O2 -Wall -Wextra -o hat_pace_selftest \
 *       tools/hat_pace_selftest.c -lpthread
 *   ./hat_pace_selftest [T1 T2 T3 T4 T5 T6]      (default: all)
 *
 * Exit 0 = every assertion passed. "RED" checks assert that the v1
 * violation PERSISTS (they must pass too — that is the pinned repro).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* wire contract (engine v3.6, frozen): 48 kHz mono s16le */
#define WIRE_RATE 48000u
#define SAMPBYTES 2u
/* client format under test (aplay -D hat wav: 44.1 kHz stereo) */
#define CLIENT_RATE 44100u
/* libasound instance geometry (1.2.14 defaults for this setup) */
#define BUF_MS     400
#define PER_MS     100
#define BUF_FRAMES ((uint64_t)(CLIENT_RATE * BUF_MS / 1000)) /* 17640 */
#define PER_FRAMES ((uint64_t)(CLIENT_RATE * PER_MS / 1000)) /* 4410 */
#define AVAIL_MIN  PER_FRAMES  /* libasound default wait gate */
#define START_TSH  PER_FRAMES  /* auto-start threshold */
/* transport budgets */
#define V1_SNDBUF  (64 * 1024) /* v1 product HAT_SNDBUF (advisory) */
#define V2_SNDBUF  (16 * 1024) /* v2 redesign */
#define ENG_RCVBUF (32 * 1024) /* engine side, both */
#define WATCHDOG_MS 600u       /* stalled-engine detection bound */
/* fake engine ring: 8192 B = 4096 samples = 167 ms behind the chew */
#define RING_BYTES 8192u
/* resampler fixed point (identical to the v1 plugin) */
#define LIN_SHIFT 19
#define LIN_DIV  (1u << LIN_SHIFT)
#define PITCH_FIXED ((uint64_t)WIRE_RATE * LIN_DIV / CLIENT_RATE)
/* 48000 -> 44100 reduced fraction = 147/160 (chewed samples -> frames) */
#define W2C_NUM 147ull
#define W2C_DEN 160ull
/* expected wire sample counts for the test files */
#define WIRE_2S  96000u /* 2.000 s @ 48 kHz */
#define WIRE_03S 14399u /* 13230 client frames -> 14399.89 -> 14399 */
/* one engine ring of chew time (8192 B @ 48 kHz mono = 168.4 ms) + slack */
#define RING_MS_NS (180ull * 1000000ull)

/* ------------------------------------------------------------------ */
/* time helpers                                                        */
/* ------------------------------------------------------------------ */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}
static void sleep_us(long us)
{
    struct timespec ts = { us / 1000, (us % 1000) * 1000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

/* ------------------------------------------------------------------ */
/* fake engine (v3.6 contract: SOCK_STREAM, one conn = one job,        */
/* continuous feeder -> fixed ring, 48 kHz tick consumes the ring;     */
/* PAUSE freezes the FEED ONLY — the frozen daemon keeps ticking at    */
/* 48 kHz off an empty ring (the audible underrun); DIE closes the     */
/* job + listener and optionally rebinds (daemon restart)              */
/* ------------------------------------------------------------------ */
#define ENT_MAX 16384
typedef struct engine {
    char path[128];
    pthread_t tid;
    volatile int running;
    int listen_fd;
    volatile uint64_t pause_until_ns;
    volatile uint64_t pause_ns; /* scenario stall length (128-bit-safe pair) */
    volatile int die_now;
    volatile int restart;
    /* counters: totals across jobs, last_* per last job. `jobs` (atomic)
 * is incremented the moment a connection is accepted (the marker the
 * client waits on — the per-job byte counters are published at teardown,
 * which can LAG the next accept during a lazy reconnect); `jobs_done`
 * trails it by the in-flight job. */
    atomic_uint_fast64_t jobs;
    uint64_t jobs_done, bytes_read, emitted, underrun_ticks;
    uint64_t last_bytes, last_emitted, last_underrun;
    /* live consumption of the CURRENT job (the tick loop is the sole
     * writer; the client reads it under no lock at close for the
     * over-claim witness). NULL-equivalent (0) between jobs. */
    volatile uint64_t live_emitted;
    volatile int live_job;
    /* delivery ladder of the CURRENT job: (cumulative bytes in ring,
     * ring-entry t_ns), ascending; the model reads it to time chunk
     * chew starts against the engine's real consumption */
    pthread_mutex_t ent_mtx;
    uint64_t ent_cum[ENT_MAX];
    uint64_t ent_t[ENT_MAX];
    int ent_n;
    /* current job's delivered-byte evidence for the v2 model's
     * evidence cap (stored before the job's feeder can deliver, read
     * under *cur_mtx while the job is live, cleared at teardown;
     * NULL between jobs) */
    uint64_t *cur_fed;
    pthread_mutex_t *cur_mtx;
    /* one-shot underrun-start marker (the buzz is audible ONCE per
     * freeze; the engine logs it the first time the ring drains to
     * empty with feed still pending) */
    volatile int underrun_ever;
    volatile int underrun_ever_logged;
} engine_t;

typedef struct jobctx {
    int fd;
    engine_t *e;
    uint8_t ring[RING_BYTES];
    volatile uint32_t rin, rout, vol_n; /* byte ring; vol_n = bytes in */
    volatile int stop, eof;
    pthread_t ftid;
    uint64_t fed; /* bytes moved into the ring (== delivered); GUARDED by
                   * ring_mtx — the feeder thread and the tick loop used
                   * to race on plain volatiles and the torn pair (a
                   * ring byte count read next to a volume count) showed
                   * up as phantom lost samples */
    pthread_mutex_t ring_mtx; /* guards ring/rin/rout/vol_n/fed */
} jobctx_t;

/* transient hang debug (state-change + low-rate logs to a file) */
static FILE *dbgf(void)
{
    static FILE *f;
    if (!f) {
        f = fopen("/tmp/hps_dbg.log", "a");
        setvbuf(f, NULL, _IOLBF, 0);
    }
    return f;
}
static void dbgv(const char *pref, const char *msg)
{
    FILE *f = dbgf();
    if (f)
        fprintf(f, "t=%.3f [%s] %s\n", now_s(), pref, msg);
}
/* log-only-on-state-change */
static void dbgstate(int *st, int ns, const char *msg)
{
    if (*st == ns)
        return;
    *st = ns;
    dbgv("feed", msg);
}
static void *engine_feed(void *argp)
{
    jobctx_t *j = argp;
    uint8_t tmp[2048];
    int st = -1;

    for (;;) {
        if (j->stop || j->eof || !j->e->running || j->e->die_now) {
            dbgv("feed", "exit loop (stop/eof/running/die)");
            break;
        }
        if (now_ns() < j->e->pause_until_ns) {
            /* THE stall: the FEED freezes (socket bytes wait in the
             * kernel buffer) while the TICK LOOP KEEPS RUNNING — a
             * frozen daemon's consume thread keeps pulling at 48 kHz
             * off whatever the ring holds, so a feed stall drains the
             * ring to underrun (the audible DC buzz). The tick clock
             * must therefore never pause or re-anchor; due() from the
             * job start IS the wall-time consumption. */
            dbgstate(&st, 9, "feed stalled (tick loop keeps running)");
            sleep_ms(5);
            continue;
        }
        ssize_t r = recv(j->fd, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (r > 0) {
            dbgstate(&st, 0, "recv>0");
            /* the wire is whole 48 kHz samples: the client (and the real
             * v2 plugin) never sends a dangling byte, so an ODD byte
             * count here is a test bug, not engine behaviour */
            if (r & 1) {
                dbgv("feed",
                     "ODD recv (bug: client sent a dangling byte)");
                j->eof = 1;
                break;
            }
            uint64_t rem = (uint64_t)r, k = 0;
            while (rem > 0) {
                if (j->stop || j->eof || !j->e->running ||
                    j->e->die_now)
                    return NULL;
                /* one locked step: copy into the ring exactly as much
                 * as the ticks can consume whole, so vol_n and the
                 * pointers can never end on an odd byte */
                pthread_mutex_lock(&j->ring_mtx);
                uint32_t free_n = (uint32_t)RING_BYTES - j->vol_n;
                free_n &= ~1u;
                uint32_t c = (rem < free_n) ? (uint32_t)rem : free_n;
                c &= ~1u; /* never a dangling half-sample */
                if (c > 0) {
                    uint32_t tail =
                        ((uint32_t)RING_BYTES - j->rin) & ~1u;
                    if (c > tail)
                        c = tail;
                    memcpy(j->ring + j->rin, tmp + k, c);
                    j->rin = (j->rin + c) & ((uint32_t)RING_BYTES - 1);
                    j->vol_n += c;
                    k += c;
                    rem -= c;
                    j->fed += c;
                }
                uint64_t fed_now = j->fed;
                pthread_mutex_unlock(&j->ring_mtx);
                if (c == 0) {
                    dbgstate(&st, 1, "ring full: sleep");
                    sleep_ms(2); /* ring full: wait (the daemon does) */
                    continue;
                }
                /* delivery ladder: byte `fed_now` entered the ring now;
                 * the feeder keeps it so the ladder is a contiguous, in
                 * order, locked prefix (lock held across both, so no
                 * torn (cum,t) pair is ever published) */
                pthread_mutex_lock(&j->e->ent_mtx);
                if (j->e->ent_n < ENT_MAX) {
                    j->e->ent_cum[j->e->ent_n] = fed_now;
                    j->e->ent_t[j->e->ent_n] = now_ns();
                    j->e->ent_n++;
                }
                pthread_mutex_unlock(&j->e->ent_mtx);
            }
            continue;
        }
        if (r == 0) {
            dbgv("feed", "EOF (recv=0)");
            j->eof = 1;
            break;
        }
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            dbgv("feed", "recv error non-eagain: break");
            break;
        }
        dbgstate(&st, 2, "recv EAGAIN");
        sleep_us(250);
    }
    return NULL;
}

/* ring-entry time of byte #off of the CURRENT job's delivery stream;
 * 0 = that byte has not entered the ring yet. */
static uint64_t engine_entry_time(engine_t *e, uint64_t off)
{
    uint64_t t = 0;
    int lo = 0, hi;

    pthread_mutex_lock(&e->ent_mtx);
    hi = e->ent_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (e->ent_cum[mid] > off) {
            t = e->ent_t[mid];
            hi = mid - 1;
        } else
            lo = mid + 1;
    }
    pthread_mutex_unlock(&e->ent_mtx);
    return t;
}

static int engine_listen(engine_t *e)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    size_t n = offsetof(struct sockaddr_un, sun_path) + strlen(e->path);
    memcpy(sa.sun_path, e->path, strlen(e->path));
    unlink(e->path);
    if (bind(fd, (struct sockaddr *)&sa, n) < 0 || listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    e->listen_fd = fd;
    return 0;
}

static void *engine_fn(void *argp)
{
    engine_t *e = (engine_t *)argp;

    for (;;) {
        if (!e->running)
            break;
        if (e->die_now) {
            e->die_now = 0;
            if (!e->restart)
                break; /* daemon died for good */
            e->restart = 0;
            if (e->listen_fd >= 0) {
                close(e->listen_fd);
                e->listen_fd = -1;
            }
            unlink(e->path);
            /* daemon restart: rebind when the slot is free */
            for (int i = 0; i < 400; i++) {
                if (engine_listen(e) == 0)
                    break;
                sleep_ms(5);
            }
            continue;
        }
        if (e->listen_fd < 0 && engine_listen(e) != 0) {
            sleep_ms(5);
            continue;
        }
        struct pollfd pf = { .fd = e->listen_fd, .events = POLLIN };
        int pr = poll(&pf, 1, 10);
        if (pr <= 0)
            continue;
        int job = accept(e->listen_fd, NULL, NULL);
        if (job < 0)
            continue;

        (void)!setsockopt(job, SOL_SOCKET, SO_RCVBUF,
                          &(int){ ENG_RCVBUF }, sizeof(int));
        /* accept-time connection marker: the client waits on
         * jobs_done for this JOB's counters, but a scenario check that
         * only needs "a connection was opened" (lazy reconnect) uses
         * this atomic, which does not trail teardown */
        atomic_fetch_add_explicit(&e->jobs, 1, memory_order_relaxed);
        pthread_mutex_lock(&e->ent_mtx);
        e->ent_n = 0; /* fresh job: fresh delivery ladder */
        pthread_mutex_unlock(&e->ent_mtx);
        jobctx_t j = { 0 };
        j.fd = job;
        j.e = e;
        pthread_mutex_init(&j.ring_mtx, NULL);
        dbgv("engine", "job accepted fd=");
        {
            FILE *f = dbgf();
            if (f)
                fprintf(f, "(fd=%d)\n", job);
        }
        if (pthread_create(&j.ftid, NULL, engine_feed, &j) != 0) {
            close(job);
            continue;
        }
        /* the v2 model may read the job's delivered-bytes evidence
         * under its lock while the job is live (evidence cap) */
        e->cur_fed = &j.fed;
        e->cur_mtx = &j.ring_mtx;

        uint64_t t0 = now_ns(), tick_done = 0, emitted = 0,
                 underrun = 0;
        int underrun_started = 0;
        /* live consumption witness (the engine is serial: while this
         * job runs it is THE job) */
        e->live_emitted = 0;
        e->live_job = 1;
        for (;;) {
            if (!e->running || e->die_now)
                break;
            /* NO PAUSE HERE (fidelity): a frozen daemon keeps its
             * 48 kHz consume thread spinning off an empty ring during
             * a feed stall — every empty tick is an underrun (hold
             * last sample = the DC buzz). A tick loop that froze with
             * the feed (a) hid that signal (0 underrun ticks through
             * a 1 s stall) and (b) re-anchored the consumption clock
             * — two clocks, two pauses, one freeze past the wall:
             * the earlier "engine never resumes" dead-end. due() from
             * the job start, at exactly WIRE_RATE, is the whole story.
             * An empty ring is consumed at tick speed, so `due` falls
             * behind and the loop simply keeps underrunning. */
            /* paced consumption: one 48 kHz ring sample per tick; the
             * tick clock IS the engine's 48 kHz sample rate (real
             * engine: 192 kHz spin, OS=4). Empty tick = 1 underrun
             * tick (real engine: hold last sample, g_underrun++).
             * due() counts samples due SINCE t0 at exactly WIRE_RATE,
             * so consumption runs at wall time. The ring is walked
             * under ring_mtx (the feeder's lock), so (vol_n, rout,
             * fed) can never be torn and consumption takes whole
             * samples only. */
            uint64_t due = (now_ns() - t0) * WIRE_RATE / 1000000000ull;
            int jobend = 0;
            while (tick_done < due) {
                pthread_mutex_lock(&j.ring_mtx);
                if (j.vol_n >= SAMPBYTES) {
                    j.rout = (uint32_t)((j.rout + SAMPBYTES) %
                                        RING_BYTES);
                    j.vol_n -= SAMPBYTES;
                    emitted++;
                } else
                    underrun++;
                if (j.eof && j.vol_n == 0)
                    jobend = 1;
                pthread_mutex_unlock(&j.ring_mtx);
                tick_done++;
                e->live_emitted = emitted; /* live witness (no lock:
                                             * torn reads harmless —
                                             * the witness is a bound) */
                if (jobend)
                    break;
            }
            if (jobend) {
                dbgv("engine", "job done (eof + ring empty)");
                break; /* EOF delivered and the ring is chewed out */
            }
            /* one-shot underrun marker: the ring drained empty WHILE
             * bytes were still pending in the feed — the exact moment
             * a live listener would hear the buzz start (state-change
             * line, logged once per job) */
            if (!underrun_started) {
                pthread_mutex_lock(&j.ring_mtx);
                uint32_t uv = j.vol_n;
                uint64_t uf = j.fed;
                int ueof = j.eof;
                pthread_mutex_unlock(&j.ring_mtx);
                if (uv == 0 && uf > 0 && !ueof) {
                    pthread_mutex_lock(&e->ent_mtx);
                    if (!e->underrun_ever) {
                        e->underrun_ever = 1;
                    }
                    pthread_mutex_unlock(&e->ent_mtx);
                    if (!e->underrun_ever_logged) {
                        e->underrun_ever_logged = 1;
                        dbgv("engine", "underrun start: ring empty, feed "
                                       "still pending (DC buzz audible");
                        { FILE *f = dbgf();
                          if (f)
                              fprintf(f,
                                      " fed=%lu t=+%.3fs\n",
                                      (unsigned long)uf,
                                      (now_ns() - t0) / 1e9);
                        }
                    }
                }
            }
            underrun_started = (underrun > 0);
            sleep_us(50);
        }
        e->live_job = 0; /* job chewed out: witness stale from here */

        j.stop = 1;
        close(job); /* unblocks the feeder's recv */
        dbgv("engine", "job fd closed, joining feeder");
        pthread_join(j.ftid, NULL);
        e->cur_fed = NULL; /* j is about to go out of scope */
        e->cur_mtx = NULL;
        pthread_mutex_destroy(&j.ring_mtx);
        /* publish the per-job counters first, THEN the completion
         * marker the client waits on (jobs_done): the wait loop in
         * run_client returns the instant jobs_done advances, so it
         * must always see the last_* counters of THAT job. The engine
         * fn thread is the sole writer of all of these. `jobs` was
         * already counted at accept (the connection marker). */
        e->last_bytes = j.fed;
        e->last_emitted = emitted;
        e->last_underrun = underrun;
        e->bytes_read += j.fed;
        e->emitted += emitted;
        e->underrun_ticks += underrun;
        e->jobs_done++;
    }
    if (e->listen_fd >= 0) {
        close(e->listen_fd);
        e->listen_fd = -1;
    }
    unlink(e->path);
    e->running = 0;
    return NULL;
}

static int engine_start(engine_t *e, const char *dir)
{
    memset(e, 0, sizeof(*e));
    e->listen_fd = -1;
    snprintf(e->path, sizeof(e->path), "%s/hat_selftest_%d.sock", dir,
             (int)getpid());
    pthread_mutex_init(&e->ent_mtx, NULL);
    e->running = 1;
    if (pthread_create(&e->tid, NULL, engine_fn, e) != 0)
        return -1;
    for (int i = 0; i < 200 && e->listen_fd < 0; i++)
        sleep_ms(5);
    if (e->listen_fd < 0) {
        e->running = 0;
        e->die_now = 1;
        pthread_join(e->tid, NULL);
        return -1;
    }
    return 0;
}

static void engine_stop(engine_t *e)
{
    if (!e->running)
        return;
    e->running = 0; /* clean exit at the next loop point */
    pthread_join(e->tid, NULL);
    unlink(e->path);
    pthread_mutex_destroy(&e->ent_mtx);
}

/* SIGALRM-driven scenario control (plain volatile stores: safe). */
static engine_t *g_eng;
static volatile int g_phase; /* 1 = pause 600 ms, 2 = die + rebind */
static void on_alarm(int sig)
{
    (void)sig;
    if (g_phase == 1 && g_eng) {
        uint64_t t = now_ns();
        /* write the pair duration-then-until: until is read only AFTER
         * pause_ns in this harness (main sets both before arming) */
        g_eng->pause_until_ns = t + g_eng->pause_ns;
    } else if (g_phase == 2 && g_eng) {
        g_eng->die_now = 1;
        g_eng->restart = 1;
    }
    g_phase = 0;
}

/* ------------------------------------------------------------------ */
/* model (v1: verbatim consumption bookkeeping; v2: gated schedule)    */
/* ------------------------------------------------------------------ */
struct model {
    int kind; /* 0 = v1, 1 = v2 */
    char path[128];
    int fd;    /* engine connection, -1 = off */
    int dead;  /* transport failure seen */
    uint64_t last_prog_ns; /* watchdog base (v2) */
    /* v1 accounting — field-for-field the plugin's hat_state slice */
    uint64_t mass; /* 48 kHz samples handed to the socket */
    uint64_t drain; /* wall ns at which `mass` is consumed */
    int64_t sent;  /* client frames "consumed" (== pointer, DIV-scaled) */
    uint64_t acc; /* 48->client frame remainder, LIN_DIV scale */
    /* v2 schedule: accepted chunks (job-relative delivery offsets) */
    struct {
        uint64_t byte_off; /* first byte of this chunk in the job */
        uint64_t nbytes;
    } chunk[16384];
    uint64_t nchunk, sent_wbytes; /* wire bytes accepted (total) */
    /* resampler carry — identical in both models */
    uint64_t rsacc;
};
/* v2 chew-front state (shared by pointer and drain-done) */
typedef struct {
    uint64_t i;     /* first not-fully-chewed chunk */
    uint64_t end;   /* chew-end ns of chunk i-1 */
    uint64_t chewed; /* wire samples chewed (whole chunks) */
} v2_front_t;
/* evidence cap: the chew front can never outrun the bytes DELIVERED
 * TO THE RING (fed). The chew schedule is wall-clock (entry times +
 * chunk durations), so after a delivery GAP it claims consumption the
 * engine cannot have made — the v1 sin in v2 clothing. Engine chew is
 * exactly fed - (bytes in ring), and the in-flight partial can only
 * consume what is in the ring, so fed is the exact evidence bound.
 * The delivered bytes are the strongest consumption evidence a plugin
 * can have (the product gets it from a future engine counter channel;
 * with the FROZEN v3.6 engine, which exposes none, a stall shorter
 * than the socket budget (~0.83 s here) is lost silently — budget-
 * proven, documented in the journal). */
static v2_front_t v2f;
/* v2 pointer-side stall state (the transport-side watchdog in
 * model_push ONLY covers in-flight send errors; the client gate can
 * hold the caller while the socket is below full and the engine
 * simply does not consume — the front is the only witness). */
static uint64_t v2_front_val;  /* last observed chew front (wire samples) */
static uint64_t v2_front_ns;   /* first time `v2_front_val` was observed */
static int v2_front_have;

static struct model model = { .fd = -1 }; /* single instance; the core rides
                                           * on it (.fd = -1: zero-init would
                                           * leave fd 0 open and the first
                                           * reset would close stdin) */


/* client frames -> wire samples with exact carry (the plugin's
 * resampler invariant: output never exceeds what the ratio consumes) */
static uint64_t rs_samples(uint64_t nframes)
{
    uint64_t tot = model.rsacc + nframes * PITCH_FIXED;
    uint64_t out = tot >> LIN_SHIFT;
    model.rsacc = tot - (out << LIN_SHIFT);
    return out;
}

static int model_connect(void)
{
    if (model.fd >= 0)
        return 0;
    uint64_t deadline = now_ns() + 2000000000ull; /* ~2 s to find engine */
    for (;;) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -EIO;
        struct sockaddr_un sa = { .sun_family = AF_UNIX };
        size_t n =
            offsetof(struct sockaddr_un, sun_path) + strlen(model.path);
        memcpy(sa.sun_path, model.path, strlen(model.path));
        if (connect(fd, (struct sockaddr *)&sa, n) == 0) {
            int sb = (model.kind == 0) ? V1_SNDBUF : V2_SNDBUF;
            (void)!setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sb, sizeof(sb));
            if (model.kind == 1) {
                int fl = fcntl(fd, F_GETFL, 0);
                (void)!fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            }
            int asb = 0;
            socklen_t sl = sizeof(asb);
            (void)!getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &asb, &sl);
            model.fd = fd;
            model.last_prog_ns = now_ns();
            dbgv("model", "connect ok kind=");
            {
                FILE *f = dbgf();
                if (f)
                    fprintf(f, "(kind=%d fd=%d SNDBUF got=%d want=%d)\n",
                            model.kind, fd, asb, sb);
            }
            return 0;
        }
        close(fd);
        if (model.kind == 0 || now_ns() >= deadline)
            return -EIO; /* v1: single attempt, exactly like hat_connect() */
        sleep_ms(10);
    }
}

/* prepare: the v2 product stop-closes (a fresh engine job per start,
 * dropping any socket tail); the harness models that for both kinds so
 * the ONLY difference under test is the pointer/transport math. */
static void model_reset(void)
{
    if (model.fd >= 0) {
        close(model.fd); /* EOF ends the engine's current job */
        model.fd = -1;
    }
    model.dead = 0;
    model.mass = 0;
    model.drain = 0;
    model.sent = 0;
    model.acc = 0;
    model.nchunk = 0;
    model.sent_wbytes = 0;
    model.rsacc = 0;
    model.last_prog_ns = now_ns();
    memset(&v2f, 0, sizeof(v2f));
    /* v2 chew-front state is PER JOB (offsets/ladder are job-relative
     * and `end` is an absolute wall instant that is only comparable
     * within one job): a prepare-then-reconnect (XRUN recovery) MUST
     * start with a clean front, else the new job's chunk offsets
     * collide with the old job's ladder entries. The pointer's
     * stall-clock rides the same boundary. */
    v2_front_have = 0;
}

/* v2 pointer-side stall: a schedule is PENDING (accepted ahead of the
 * chew front) and the front has not advanced for WATCHDOG_MS. While
 * the client gate holds the sender, no send() ever runs to trip the
 * transport watchdog — the front is the only witness to an engine
 * that accepts but does not consume. Healthy runs are immune: with a
 * live engine the front advances continuously (ladder entries keep
 * landing); once the schedule is fully chewed there is no pending and
 * the check cannot fire */
static int v2_front_stalled(uint64_t chewed, uint64_t now)
{
    int pending = chewed < model.sent_wbytes / SAMPBYTES;
    if (!v2_front_have || chewed != v2_front_val) {
        v2_front_val = chewed;
        v2_front_ns = now;
        v2_front_have = 1;
        return 0;
    }
    if (pending &&
        now - v2_front_ns >= (uint64_t)WATCHDOG_MS * 1000000ull) {
        dbgv("model", "v2 pointer stall: front frozen");
        {
            FILE *f = dbgf();
            if (f)
                fprintf(f, " chewing=%llu sent=%llu ms_frozen=%llu\n",
                        (unsigned long long)chewed,
                        (unsigned long long)(model.sent_wbytes / SAMPBYTES),
                        (unsigned long long)((now - v2_front_ns) / 1000000ull));
        }
        return 1;
    }
    return 0;
}

/* v1 stall check: there is none (fidelity). v2: in-flight work whose
 * last full-chunk acknowledgement is older than WATCHDOG_MS == a
 * wedged transport (engine accepts but consumes nothing, or is gone). */
static int model_v2_stalled(void)
{
    if (model.nchunk > 0 &&
        now_ns() - model.last_prog_ns >
            (uint64_t)WATCHDOG_MS * 1000000ull) {
        model.dead = 1;
        return 1;
    }
    return 0;
}

/* --- v1 pointer: VERBATIM from tools/hat_alsa_plugin.c hat_pointer() */
static int64_t pointer_v1(void)
{
    uint64_t now, w, f, saved;

    if (model.dead)
        return -EIO;
    if (model.mass == 0)
        return model.sent;
    now = now_ns();
    if (now >= model.drain)
        w = model.mass;
    else {
        uint64_t rem =
            (model.drain - now) * WIRE_RATE / 1000000000ull;
        w = model.mass - (rem > model.mass ? model.mass : rem);
    }
    f = ((w * (uint64_t)CLIENT_RATE) / WIRE_RATE) * LIN_DIV;
    saved = ((uint64_t)(model.sent < 0 ? 0 : model.sent) * LIN_DIV) +
            model.acc;
    if (f > saved) {
        uint64_t d = f - saved;
        model.sent += (int64_t)(d >> LIN_SHIFT);
        model.acc = d & (LIN_DIV - 1);
    }
    return model.sent;
}

/* --- v2 pointer: serial send-side schedule. Position = where the
 * engine's consumption front is: each accepted chunk chews at the wire
 * clock starting at its RING-ENTRY time (bytes delivered to the engine),
 * back-to-back behind the previous chunk (one engine, one pipe).
 * Monotone by construction; never exceeds accepted. */
static uint64_t v2_chewed_wire(uint64_t now)
{
    uint64_t n = model.nchunk;
    uint64_t chewed = v2f.chewed; /* only FULLY chewed chunks (persisted) */

    while (v2f.i < n) {
        uint64_t dur = model.chunk[v2f.i].nbytes * 1000000000ull /
                       (WIRE_RATE * SAMPBYTES);
        uint64_t entry =
            engine_entry_time(g_eng, model.chunk[v2f.i].byte_off);
        if (entry == 0)
            break; /* first bytes not in the ring yet */
        uint64_t start = entry > v2f.end ? entry : v2f.end;
        if (now < start + dur)
            break;
        chewed += model.chunk[v2f.i].nbytes / SAMPBYTES;
        v2f.end = start + dur;
        v2f.i++;
        v2f.chewed = chewed; /* persist the fully-chewed total */
    }
    /* in-flight partial: computed FRESHLY from the current chunk's
     * absolute start (never accumulated into the persisted state) */
    uint64_t el = chewed; /* elapsed-model chew front, wire samples */
    if (v2f.i < n) {
        uint64_t dur = model.chunk[v2f.i].nbytes * 1000000000ull /
                       (WIRE_RATE * SAMPBYTES);
        uint64_t entry = engine_entry_time(g_eng, model.chunk[v2f.i].byte_off);
        if (entry != 0) {
            uint64_t start = entry > v2f.end ? entry : v2f.end;
            if (now > start) {
                uint64_t p = now - start;
                if (p > dur)
                    p = dur;
                el = chewed + p * WIRE_RATE / 1000000000ull;
            }
        }
    }
    /* evidence cap (see above): the delivered-bytes count is read
     * under the job's ring lock; frozen delivery freezes the front
     * (worst case within one ring of chew time of the stall start),
     * and the pointer's stall check turns the frozen front into -EIO
     * once it has been frozen for the watchdog */
    if (el > 0 && g_eng && g_eng->cur_fed && g_eng->cur_mtx) {
        pthread_mutex_lock(g_eng->cur_mtx);
        uint64_t ev = *g_eng->cur_fed / SAMPBYTES;
        pthread_mutex_unlock(g_eng->cur_mtx);
        if (el > ev)
            el = ev;
    }
    return el;
}

static int64_t pointer_v2(void)
{
    uint64_t now, chewed;

    if (model.dead)
        return -EIO;
    if (model.nchunk == 0)
        return 0;
    now = now_ns();
    chewed = v2_chewed_wire(now);
    /* a stalled engine shows up here as a frozen chew front with a
     * pending schedule; the caller turns -EIO into the recoverable
     * XRUN (the engine keeps chewing, the content re-sends) */
    if (v2_front_stalled(chewed, now)) {
        model.dead = 1;
        return -EIO;
    }
    if (chewed > 0x7fffffffu)
        chewed = 0x7fffffffu;
    return (int64_t)(chewed * W2C_NUM / W2C_DEN);
}

/* v2 drain completion: the whole accepted schedule is chewed AND one
 * engine ring of chew time has passed (the ring may still hold
 * delivered bytes behind the model's chew front; the journal's "2x
 * remaining" drain bound dominates that margin in the product). */
static int model_v2_done(void)
{
    if (model.dead)
        return 1; /* the drain loop surfaces it as an error */
    if (model.nchunk == 0)
        return 1;
    uint64_t ch = v2_chewed_wire(now_ns());
    if (ch < model.sent_wbytes / SAMPBYTES)
        return 0;
    return now_ns() >= v2f.end + RING_MS_NS;
}

static int64_t model_pointer(void)
{
    return model.kind == 0 ? pointer_v1() : pointer_v2();
}

/* transfer: move `frames` client frames to the engine.
 * Returns +frames (accepted), 0 (resample carry: no wire output yet),
 * -EAGAIN (v2: queue full, retriable), or -EPIPE/-EIO (transport dead). */
static int64_t model_push(uint64_t frames)
{
    uint64_t n_out = rs_samples(frames);
    if (n_out == 0)
        return 0;
    if (model.fd < 0 && model_connect() != 0)
        return model.kind == 0 ? -EPIPE : -EIO;

    uint8_t stage[64 * 1024]; /* content: zeros (the engine counts) */
    uint64_t nbytes = n_out * SAMPBYTES, off = 0;

    if (model.kind == 0) {
        /* v1: blocking send of the whole chunk (hat_transfer shape) */
        while (off < nbytes) {
            ssize_t w = send(model.fd, stage + off, nbytes - off,
                             MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                dbgv("model", "v1 send failed:");
                {
                    FILE *f = dbgf();
                    if (f)
                        fprintf(f, " errno=%d after %lu B\n",
                                (int)errno, (unsigned long)off);
                }
                model.dead = 1;
                return -EPIPE; /* the plugin's recoverable-underrun */
            }
            off += (uint64_t)w;
        }
        /* VERBATIM v1 update (hat_transfer tail): the drain anchor
         * tracks ONLY the last chunk's wire duration — correct solely
         * while the engine is continuously fed from the socket. */
        uint64_t now = now_ns();
        uint64_t dur_ns =
            nbytes / SAMPBYTES * 1000000000ull / WIRE_RATE;
        if (model.drain > now && model.mass > 0)
            model.drain += dur_ns;
        else
            model.drain = now + dur_ns;
        model.mass += nbytes / SAMPBYTES;
        model.sent_wbytes += nbytes;
        model.last_prog_ns = now;
        return (int64_t)frames;
    }

    /* v2: nonblocking send of the whole chunk, bounded by the watchdog */
    uint64_t block_start = 0;
    while (off < nbytes) {
        ssize_t w = send(model.fd, stage + off, nbytes - off, MSG_NOSIGNAL);
        if (w > 0) {
            off += (uint64_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0 && (errno == EPIPE || errno == ECONNRESET ||
                      errno == ENOTCONN)) {
            model.dead = 1;
            return -EIO;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (block_start == 0)
                block_start = now_ns();
            uint64_t elapsed_ms =
                (now_ns() - block_start) / 1000000ull;
            if (elapsed_ms >= WATCHDOG_MS) {
                if (model_v2_stalled()) {
                    model.dead = 1;
                    return -EIO;
                }
                return -EAGAIN; /* soft pause: aplay -> snd_pcm_wait */
            }
            struct pollfd pf = { .fd = model.fd,
                                 .events = POLLOUT | POLLERR | POLLHUP };
            /* poll for the balance of the watchdog budget; it also
             * absorbs pause/die events that land mid-wait */
            int pr = poll(&pf, 1, (int)(WATCHDOG_MS - elapsed_ms));
            if (pr < 0 && errno != EINTR) {
                model.dead = 1;
                return -EIO;
            }
            if (pr > 0 && (pf.revents & (POLLERR | POLLHUP))) {
                model.dead = 1;
                return -EIO; /* dead socket; aplay -> XRUN */
            }
            continue; /* woke: retry send now (or still none -> EAGAIN) */
        }
        model.dead = 1;
        return -EIO;
    }
    /* whole chunk accepted by the kernel: record its JOB-RELATIVE
     * delivery offset. Chew timing comes from the engine's delivery
     * ladder (engine_entry_time on byte_off): the chunk's first byte
     * enters the ring once the feeder's locked prefix covers it. No
     * model-side wall-clock assumptions about the engine. */
    if (model.nchunk < sizeof(model.chunk) / sizeof(model.chunk[0])) {
        model.chunk[model.nchunk].byte_off = model.sent_wbytes;
        model.chunk[model.nchunk].nbytes = nbytes;
        model.nchunk++;
    }
    model.sent_wbytes += nbytes;
    model.last_prog_ns = now_ns();
    return (int64_t)frames;
}

/* ------------------------------------------------------------------ */
/* mini-core: alsa-lib 1.2.14 semantics for this instance              */
/* ------------------------------------------------------------------ */
typedef struct core {
    int state; /* 0 SETUP, 1 PREPARED, 2 RUNNING, 3 XRUN, 4 DRAINING */
    uint64_t appl, hw;
    /* per-run metrics */
    int64_t minavail, maxavail;
    int have_avail;
    uint64_t ptr_backwards; /* pointer stepped backwards (the contract) */
    int64_t lastptr;
    int have_lastptr;
    int xruns; /* transfer/hwsync -> XRUN count (client-visible) */
    int xruns_total; /* cumulative across this run's prepares */
} core_t;
static core_t core;

/* snd_pcm_prepare -> ioplug reset: appl = hw = last_hw = 0 */
static void core_prepare(void)
{
    model_reset();
    core.state = 1;
    core.appl = core.hw = 0;
    core.have_avail = 0;
    core.xruns = 0;
    core.ptr_backwards = 0;
    core.lastptr = 0;
    core.have_lastptr = 0;
}

/* snd_pcm_ioplug_hwsync: pointer < 0 -> XRUN (drop while DRAINING).
 * pointer >= 0 -> hw advances by the delta against the LAST PLUGIN
 * POINTER (io->last_hw); wrap = buffer_size + p - last (no BOUNDARY_WA
 * in this instance). Delta against last_hw — not against the internal
 * hw — is what keeps the first RUNNING hwsync an honest advance. */
static int core_hwsync(void)
{
    int64_t p = model_pointer();

    if (p < 0) {
        if (core.state == 4) {
            core.state = 0; /* drop: a dead stream ends the drain */
            return -EPIPE;
        }
        if (core.state != 3) {
            core.state = 3;
            core.xruns++;
            core.xruns_total++;
        }
        return -EPIPE;
    }
    if (core.have_lastptr && (uint64_t)p < (uint64_t)core.lastptr)
        core.ptr_backwards++;
    uint64_t delta = ((uint64_t)p >= (uint64_t)core.lastptr)
                         ? (uint64_t)p - (uint64_t)core.lastptr
                         : BUF_FRAMES + (uint64_t)p -
                               (uint64_t)core.lastptr;
    core.hw += delta;
    core.lastptr = p;
    core.have_lastptr = 1;
    return 0;
}

/* __snd_pcm_avail_update (non-boundary): true avail = buffer +
 * (hw - appl); NEGATIVE when the send queue exceeds the buffer. The
 * writei gate below treats negative / sub-avail_min avail (RUNNING) as
 * the wait condition -> -EAGAIN in nonblock mode. XRUN -> -EPIPE. */
static int64_t core_avail(void)
{
    int64_t avail;

    if (core.state == 3)
        return -EPIPE;
    avail = (int64_t)BUF_FRAMES +
            ((core.hw >= core.appl) ? (int64_t)(core.hw - core.appl)
                                    : -(int64_t)(core.appl - core.hw));
    if (!core.have_avail) {
        core.have_avail = 1;
        core.minavail = core.maxavail = avail;
    } else {
        if (avail < core.minavail)
            core.minavail = avail;
        if (avail > core.maxavail)
            core.maxavail = avail;
    }
    return avail;
}

/* a transfer reported the transport dead: put the stream in XRUN and
 * normalize the error to -EPIPE so the client's recover path fires */
static int64_t norm_dead(int64_t r)
{
    if (r < 0 && model.dead && core.state != 3 && core.state != 0) {
        core.state = 3;
        core.xruns++;
        core.xruns_total++;
        return -EPIPE;
    }
    return r;
}

/* snd_pcm_write_areas (nonblock mode; the blocking branch in the real
 * lib is the client's snd_pcm_wait() — same outcome: retry on space).
 * PREPARED: direct transfer, auto-start at start_threshold (no hwsync:
 * ioplug takes its hw only from the pointer callback, which the hot
 * path does not run in PREPARED). RUNNING: hwsync, then the gate. */
static int64_t core_writei(uint64_t size)
{
    uint64_t xfer = 0;
    int64_t err = 0;

    while (size > 0) {
        if (core.state == 3) {
            err = -EPIPE;
            break;
        }
        if (core.state == 2 && core_hwsync() < 0) {
            err = -EPIPE;
            break;
        }
        if (core.state == 1) {
            /* prepared: push up to a full period, start on threshold */
            uint64_t frames = size < PER_FRAMES ? size : PER_FRAMES;
            int64_t r = model_push(frames);
            if (r < 0) {
                err = norm_dead(r); /* -EPIPE for a dead transport, */
                break;              /* raw err (none in practice) else */
            }
            if (r == 0)
                break; /* resample carry: nothing to transfer */
            core.appl += (uint64_t)r;
            xfer += (uint64_t)r;
            size -= (uint64_t)r;
            if ((uint64_t)r >= START_TSH)
                core.state = 2; /* auto-start (no hw mirror in PREPARED) */
            continue;
        }
        int64_t avail = core_avail();
        if (avail < 0) {
            /* negative true avail (queue beyond buffer) or XRUN
             * (-EPIPE from avail_update) */
            err = (avail == -EPIPE) ? -EPIPE : -EAGAIN;
            break;
        }
        if (size > (uint64_t)avail && avail < (int64_t)AVAIL_MIN) {
            /* may_wait_for_avail_min: nonblock -> -EAGAIN (the client
             * snd_pcm_wait(poll)s and retries) */
            err = -EAGAIN;
            break;
        }
        uint64_t frames =
            size < (uint64_t)avail ? size : (uint64_t)avail;
        if (frames == 0) {
            err = -EAGAIN;
            break;
        }
        int64_t r = model_push(frames);
        if (r < 0) {
            err = norm_dead(r);
            break;
        }
        if (r == 0)
            break; /* carry: no wire output this chunk */
        core.appl += (uint64_t)r;
        xfer += (uint64_t)r;
        size -= (uint64_t)r;
    }
    if (err == 0)
        err = (size > 0) ? -EAGAIN : 0; /* avail exhausted or done */
    if (xfer > 0)
        return (int64_t)xfer; /* partial: the client advances on it */
    return err;
}

/* snd_pcm_drain (alsa-lib 1.2.14 ioplug: the callback's return is the
 * wait; on 0 the framework drop()s: stop + close). v1 "done" is
 * IMMEDIATE — build-3 hat_drain (tools/hat_alsa_plugin.c, VERBATIM):
 * "non-blocking by design: everything we care about is already sent;
 *  ... the engine plays the remainder of its ring then frees the pin.
 *  return 0 => drop." The tail therefore survives in the engine: this
 * fake engine's EOF path (feed to EOF, chew to empty) DOES play it —
 * that is what makes v1 SILENT: no truncation on a healthy engine, and
 * on a stalled engine the stall rides through as underrun. v2 "done"
 * = the accepted schedule fully chewed + one engine ring margin. */
static double core_drain(void)
{
    double t0 = now_s();
    core.state = 4;
    if (model.kind == 0) { /* v1: no-op (the product's hat_drain) */
        core.state = 0;
        return 0.0;
    }
    for (int spins = 0; spins < 3000; spins++) {
        if (core_hwsync() < 0)
            break;
        if (model_v2_done())
            break;
        sleep_ms(2);
    }
    core.state = 0;
    return now_s() - t0;
}

/* ------------------------------------------------------------------ */
/* aplay-shaped client (nonblock) — shared types                       */
/* ------------------------------------------------------------------ */
typedef struct run_result {
    double push_s, drain_s, total_s, detect_s; /* detect = 1st trouble */
    uint64_t pushed;
    int64_t minavail;
    uint64_t maxavail;
    uint64_t ptr_backwards;
    int xruns, failed, wedged;
    uint64_t jobs, bytes_read, emitted, underrun_ticks;
    uint64_t engine_emitted_at_close; /* engine chew at close instant */
} run_result_t;
static struct run_result *g_rr;

static void core_close(void)
{
    if (model.fd >= 0)
        close(model.fd); /* EOF: the engine feeds to EOF, THEN chews */
    model.fd = -1;
    /* the engine's consumption AT the close instant: the job is still
     * alive at close (it chews the tail AFTER EOF), so the live tick
     * loop's counter is the witness — the final last_emitted that
     * run_client reads later lags close by the tail's own chew time,
     * i.e. it would read exactly the over-claimed amount and mask the
     * defect. The witness for the v1 overclaim (claimed-done at
     * close, engine still chewing) */
    if (g_rr && g_eng)
        g_rr->engine_emitted_at_close =
            (g_eng->live_job == 1) ? g_eng->live_emitted : g_eng->last_emitted;
    core.state = 0;
}

static int run_client(uint64_t total_frames, double cap_s, run_result_t *r)
{
    memset(r, 0, sizeof(*r));
    g_rr = r;
    core.xruns_total = 0; /* per-RUN xrun count (a single prepare would
                            * erase xruns recovered by an earlier one
                            * in THIS run, so run_client owns the reset
                            * and core_prepare() does not touch it) */
    core_prepare();
    uint64_t off = 0, accepted = 0; /* off = advanced, accepted = pushed */
    uint64_t prev_conn = atomic_load_explicit(&g_eng->jobs,
                                               memory_order_acquire);
    double t0 = now_s();

    while (off < total_frames) {
        if (now_s() - t0 > cap_s) {
            r->wedged = 1; /* stuck with no error: the silent wedge */
            break;
        }
        uint64_t n = total_frames - off < PER_FRAMES
                         ? total_frames - off
                         : PER_FRAMES;
        int64_t w = core_writei(n);
        if (w > 0) {
            off += (uint64_t)w;
            accepted += (uint64_t)w;
            continue;
        }
        if (w == -EAGAIN) {
            /* aplay: snd_pcm_wait(handle, 100) on the poll fd */
            if (model.fd >= 0) {
                struct pollfd pf = { .fd = model.fd,
                                     .events = POLLOUT | POLLERR | POLLHUP };
                poll(&pf, 1, 100);
            } else
                sleep_ms(2);
            continue;
        }
        if (w == -EPIPE) {
            if (r->detect_s == 0)
                r->detect_s = now_s() - t0;
            core_prepare(); /* snd_pcm_recover -> prepare */
            continue; /* same logical frame: content resumes */
        }
        if (r->detect_s == 0)
            r->detect_s = now_s() - t0;
        r->failed = 1; /* -EIO / other: unrecoverable by the client */
        break;
    }
    r->push_s = now_s() - t0;
    if (!r->wedged && !r->failed) {
        r->drain_s = core_drain();
        r->total_s = r->push_s + r->drain_s;
    } else
        r->total_s = r->push_s;
    core_close(); /* v1: EOF ends the job / v2: stop-closes */
    r->pushed = accepted; /* client-side accepted frames (plugin-local
                           * appl is wrong after a mid-run prepare) */
    r->minavail = core.minavail;
    r->maxavail = core.maxavail;
    r->ptr_backwards = core.ptr_backwards;
    /* cumulative across this run's prepares (see the xruns_total
     * reset above) */
    r->xruns = core.xruns_total;
    /* engine counters: for a LAST accepted connection, wait for the
     * job to be published (jobs_done trails the connections count by
     * at most the teardown of each connection; in the reconnect
     * scenario more than one connection completes) and before
     * reading last_*. jobs = the number of connections this client
     * opened (the T4 lazy-reconnect check concerns connections, not
     * completed jobs). */
    uint64_t end_conn =
        atomic_load_explicit(&g_eng->jobs, memory_order_acquire);
    for (int i = 0; i < 5000 && g_eng->jobs_done < end_conn; i++)
        usleep(2000); /* 10 s: generous for a 5 s run + feeder EOF */
    r->jobs = (uint64_t)(end_conn - prev_conn);
    r->bytes_read = g_eng->last_bytes;
    r->emitted = g_eng->last_emitted;
    r->underrun_ticks = g_eng->last_underrun;
    return (r->failed || r->wedged) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* checks                                                              */
/* ------------------------------------------------------------------ */
static int nfail = 0;
static void check(int cond, const char *what)
{
    printf("    %-72s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        nfail++;
}
static void metrics_line(const char *tag, const run_result_t *r)
{
    printf("  [%s] push=%.2fs drain=%.2fs total=%.2fs xruns=%d failed=%d "
           "wedged=%d detect=%.2fs\n",
           tag, r->push_s, r->drain_s, r->total_s, r->xruns, r->failed,
           r->wedged, r->detect_s);
    printf("       avail true min=%ld max=%lu (buffer=%lu) "
           "ptr_backwards=%lu pushed=%lu frames\n",
           (long)r->minavail, (unsigned long)r->maxavail,
           (unsigned long)BUF_FRAMES, (unsigned long)r->ptr_backwards,
           (unsigned long)r->pushed);
    printf("       engine: jobs=%lu read=%luB last_emitted=%lu "
           "underrun=%lu ticks(48kHz)\n",
           (unsigned long)r->jobs, (unsigned long)r->bytes_read,
           (unsigned long)r->emitted, (unsigned long)r->underrun_ticks);
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int sel[6] = { 0, 0, 0, 0, 0, 0 };
    if (argc == 1)
        sel[0] = sel[1] = sel[2] = sel[3] = sel[4] = sel[5] = 1;
    run_result_t r;

    for (int i = 1; i < argc; i++)
        if (argv[i][0] == 'T' && argv[i][1] >= '1' && argv[i][1] <= '6' &&
            argv[i][2] == '\0')
            sel[argv[i][1] - '1'] = 1;
        else {
            printf("usage: %s [T1 T2 T3 T4 T5 T6]\n", argv[0]);
            return 2;
        }

    printf("hat_pace_selftest\n");
    printf("  instance: rate=%uHz ch=2 buffer=%lu frames (%d ms) "
           "period=%lu (%d ms) avail_min=%lu start_threshold=%lu\n",
           CLIENT_RATE, (unsigned long)BUF_FRAMES, BUF_MS,
           (unsigned long)PER_FRAMES, PER_MS, (unsigned long)AVAIL_MIN,
           (unsigned long)START_TSH);
    printf("  transport: v1 sndbuf=%d blocking | v2 sndbuf=%d + "
           "watchdog %d ms\n  engine: 48 kHz rcvbuf=%dB ring=%dB; "
           "pauses/dies per scenario\n",
           V1_SNDBUF, V2_SNDBUF, WATCHDOG_MS, (int)ENG_RCVBUF,
           (int)RING_BYTES);

    static engine_t eng;
    g_eng = &eng;
    signal(SIGALRM, on_alarm);
    alarm(0);

    /* ================= T1: v1, healthy engine, 2 s (RED) =========== */
    if (sel[0]) {
        printf("\nT1  v1 consumption model, healthy engine, 2.000 s "
               "file [expect RED]\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        model.kind = 0;
        run_client(2 * CLIENT_RATE, 30.0, &r);
        metrics_line("T1", &r);
        /* The numerical pin (fidel drain): the v1 mass/drain chain is
         * a PURE SEND-SIDE clock — it self-completes as soon as the
         * last blocking send() exits, with no engine feedback and no
         * stall path. At the drain-call the pointer reads the file
         * end while the engine still holds an in-flight tail (socket
         * + ring) it has NOT chewed; the run's wall is therefore the
         * file length MINUS that tail's chew time, and the pin
         * releases early (the next job would connect into a
         * still-busy engine). With the product no-op drain the tail
         * is played after close (EOF -> chew to empty): no
         * truncation on a healthy engine — the defect is the EARLY
         * done-claim + early release, witnessed by the tail the
         * engine still owes at the close instant. T6a pins the same
         * clock riding a real stall, silently. */
        uint64_t tail_at_close =
            (r.engine_emitted_at_close < WIRE_2S)
                ? WIRE_2S - r.engine_emitted_at_close
                : 0;
        double tail_ms = (double)tail_at_close / WIRE_RATE * 1000.0;
        uint64_t truth_f = r.engine_emitted_at_close * W2C_NUM / W2C_DEN;
        printf("    claim at drain-call : %lu client frames (file "
               "end, cap)\n",
               (unsigned long)(WIRE_2S * W2C_NUM / W2C_DEN));
        printf("    engine truth @ close: %lu frames chewed (%.0f ms "
               "of claimed audio still in flight; plays after close)\n",
               (unsigned long)truth_f, tail_ms);
        check(r.detect_s == 0 && r.xruns == 0 && !r.failed && !r.wedged &&
              r.ptr_backwards == 0,
              "RED (class): silent — no error, no XRUN, no backwards "
              "step");
        check(r.drain_s < 0.02,
              "RED: drain() returns immediately — bookkeeping already "
              "self-complete (the product no-op)");
        /* the in-flight tail at close is the socket backlog plus the
         * ring, at 48 kHz. It is the run's shortfall:
         * total ~= file - tail/48k. The feeder's ring-full gating
         * keeps the socket from filling to its 1.365 s budget (the
         * engine chews continuously during the push), so the backlog
         * at EOF is ~ring + in-flight (~0.4 s measured; band = 3x
         * slack around that). 'done' is contradicted by the engine at
         * the close instant */
        check(tail_at_close >= 12000 && tail_at_close <= 36000,
              "RED: done-claimed with >=0.25 s of audio still in "
              "flight at close (early release / silent over-claim)");
        check(r.total_s >= 1.25 && r.total_s <= 1.75,
              "RED: run ends early — wall = file - in-flight tail chew "
              "(no truncation: every sample still reaches the engine; "
              "same quantity as the tail check above)");
        engine_stop(&eng);
        printf("       (tail in flight at close: %lu samples = %.0f ms; "
               "drain %.3fs; total %.2fs)\n",
               (unsigned long)tail_at_close, tail_ms, r.drain_s, r.total_s);
    }

    /* ================= T2: v2, healthy engine, 2 s (GREEN) ========== */
    if (sel[1]) {
        printf("\nT2  v2 gated model, healthy engine, 2.000 s file "
               "[expect GREEN]\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        model.kind = 1;
        run_client(2 * CLIENT_RATE, 30.0, &r);
        metrics_line("T2", &r);
        check(r.failed == 0 && r.wedged == 0, "run completed cleanly");
        check(r.xruns == 0, "no XRUN in the healthy run");
        check(r.total_s >= 1.8 && r.total_s <= 3.6,
              "wall time within [0.9x, 1.8x] of the file length");
        check(r.maxavail <= BUF_FRAMES,
              "pointer never exceeds accepted (avail <= buffer_size)");
        check(r.minavail >= -34000,
              "backlog bounded by the socket budget (no runaway)");
        check(r.ptr_backwards == 0, "pointer never stepped backwards");
        check(r.emitted >= WIRE_2S - 240 && r.emitted <= WIRE_2S,
              "full pattern reached the engine (<=5 ms slack)");
        /* healthy-run underrun is EXACTLY the v2 drain's ring margin:
         * model_v2_done() waits chewed==all-sent + one RING_MS_NS, and
         * during that margin the ring is empty (eof set, feeder done),
         * so the ticks underflow. RING_MS_NS/WIRE_RATE ~ 170 ms ~ 8200
         * ticks, plus one client-poll grain. NOT a feed gap: the ring
         * stays refilled to the cap throughout the run. A much larger
         * count means the model is hiding a stall (the old tick-loop
         * freeze read 0 ticks through a 1 s stall = the falsehood
         * this check now pins against) */
        check(r.underrun_ticks >= 6000 && r.underrun_ticks <= 12000,
              "drain-margin underrun: ~one ring of chew + a poll "
              "(a model that hides stalls reads 0; a feed gap reads "
              "tens of thousands)");
        engine_stop(&eng);
    }

    /* ================= T3: v2, engine pauses 1000 ms (GREEN) ======== */
    /* 1000 ms is the real-world stall magnitude (the live incident    */
    /* is a multi-second stall; 1 s sits squarely in that class) and   */
    /* exceeds watchdog + ring holdup, so the fire is guaranteed even  */
    /* with the ring full at the freeze point. The engine PAUSES (it  */
    /* is still alive): the reconnect after the XRUN hits the SAME    */
    /* engine, which resumes chewing as soon as the alarm ends.       */
    if (sel[2]) {
        printf("\nT3  v2 gated model, engine paused 1000 ms at t=1 s "
               "[expect detect + recover]\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        model.kind = 1;
        eng.pause_ns = 1000ull * 1000000ull; /* phase-1 lasts 1000 ms */
        g_phase = 1;
        alarm(1);
        run_client(2 * CLIENT_RATE, 30.0, &r);
        alarm(0);
        metrics_line("T3", &r);
        check(r.xruns >= 1 && r.failed == 0 && r.wedged == 0,
              "stall surfaced as XRUN and the run recovered");
        /* the front freezes within one ring chew (<=170 ms) of the pause
         * start, then the 600 ms watchdog runs: fire in
         * [1.0+600ms, 1.0+170ms+600ms+client-poll slack] */
        check(r.detect_s >= 1.5 && r.detect_s <= 2.6,
              "detection within [pause+watchdog, pause+ring+watchdog+slack]");
        /* XRUN, reconnect, re-push of the unsent remainder, chew to
         * completion, drain margin */
        check(r.total_s <= 3.9, "recovered run stays within 1.8x+length");
        engine_stop(&eng);
    }

    /* ================= T4: v2, engine dies + reboots (GREEN) ======== */
    if (sel[3]) {
        printf("\nT4  v2 gated model, engine killed (reboots) at t=1 s "
               "[expect detect + lazy reconnect]\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        model.kind = 1;
        g_phase = 2;
        alarm(1);
        run_client(2 * CLIENT_RATE, 30.0, &r);
        alarm(0);
        metrics_line("T4", &r);
        check(r.xruns >= 1 && r.failed == 0 && r.wedged == 0,
              "death surfaced as XRUN; run completed via reconnect");
        check(r.detect_s >= 1.0 && r.detect_s <= 2.5,
              "death detected within bound");
        check(r.jobs >= 2, "lazy reconnect opened a second engine job");
        engine_stop(&eng);
    }

    /* ================= T5: v2, 0.3 s short file (GREEN) ============== */
    if (sel[4]) {
        printf("\nT5  v2 gated model, 0.3 s file (13230 client frames)\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        model.kind = 1;
        run_client(3 * CLIENT_RATE / 10, 30.0, &r);
        metrics_line("T5", &r);
        check(r.failed == 0 && r.wedged == 0 && r.xruns == 0,
              "short run clean (push + drain, no XRUN)");
        check(r.total_s >= 0.2 && r.total_s <= 1.5,
              "total time bounded (real drain chews the tail)");
        check(r.bytes_read >= WIRE_03S * SAMPBYTES - 2 &&
                  r.bytes_read <= WIRE_03S * SAMPBYTES + 2,
              "engine received the exact resampled pattern (14399 s16)");
        check(r.emitted >= WIRE_03S - 2 && r.emitted <= WIRE_03S + 2,
              "engine emitted the exact 48 kHz sample count");
        engine_stop(&eng);
    }

    /* ============ T6: v1 vs v2, engine hiccup (consumption stall) === */
    /* The build-3 incident class: the engine's consumption lags the   */
    /* send queue. v1 has NO path that turns "engine not consuming"    */
    /* into an error — it soaks the stall silently (the live 9-minute  */
    /* spin is this class x RT jitter x a large queue). v2's watchdog  */
    /* turns the same event into XRUN + recovery.                      */
    /*                                                                */
    /* Engine fidelity: the stall is FEED-ONLY. The feeder thread     */
    /* (the engine's socket side) stops pulling; the tick loop keeps   */
    /* spinning off the real t0 clock. That is exactly what the live   */
    /* v3.6 engine does when it stops being fed: its locked ring      */
    /* drains and the tick clock spins underrun ticks (the DC buzz) — */
    /* the consume clock never re-anchors. The old test variant froze */
    /* the tick loop too, which hid the underrun signal and broke the */
    /* wall accounting.                                               */
    if (sel[5]) {
        printf("\nT6  hiccup (1000 ms feed+chew stall at t=1 s): v1 "
               "blind vs v2 detected\n");
        if (engine_start(&eng, "/tmp") != 0) {
            printf("    engine start failed\n");
            return 2;
        }
        memcpy(model.path, eng.path, sizeof(model.path));
        /* --- v1: silent ride-through, no detection --- */
        /* v1's clock is the send-side, and its queue ceiling          */
        /* swallows the stall: actual sndbuf 131,072 B (~1.37 s) +     */
        /* actual rcvbuf 65,536 B (~0.68 s) + ring 8,192 B (~85 ms)    */
        /* ~= 2.1 s >= the 2.0 s file, so the WHOLE pattern is         */
        /* queueable and the client's send (an effective ~1.25x        */
        /* real-time wire: 192 kB over 1.6 s of wall) NEVER blocks     */
        /* on the engine — T6a push = 1.60 s === T1's healthy 1.60 s.  */
        /* The 1000 ms pause costs the client ZERO wall time.          */
        /* Meanwhile audibly: the pause is a ~915 ms engine UNDRUN     */
        /* (feeder stalls; the ring drains in ~85 ms; the tick loop    */
        /* keeps spinning off the empty ring = the DC buzz), and       */
        /* every sample is still played. drain() is the no-op          */
        /* (build-3 verbatim): the pointer was already at the cap.     */
        /* The client saw NONE of it: no error, no XRUN, no backwards  */
        /* pointer, an early 'done' with a FULL second of audio still  */
        /* to be chewed.                                               */
        model.kind = 0;
        eng.pause_ns = 1000ull * 1000000ull; /* phase-1 lasts 1000 ms */
        g_phase = 1;
        alarm(1);
        run_client(2 * CLIENT_RATE, 30.0, &r);
        alarm(0);
        metrics_line("T6a(v1)", &r);
        check(r.detect_s == 0 && r.xruns == 0 && !r.failed && !r.wedged &&
              r.ptr_backwards == 0,
              "RED (class): silent — NOTHING (no error, no XRUN, no "
              "backwards step)");
        /* the stall is audible at the engine, not at the client: the
         * tick loop (the REAL consume clock) spun off an empty ring
         * for (pause - one ring chew) ~ (1000 - 85) ms ~= 915 ms ~=
         * 44k ticks (one-shot 'underrun start' state line; the 85 ms
         * is the ring's own chew-out that masks the first fraction of
         * the pause). Band: lower = buzz minus ~0.3 s schedule slack;
         * upper = the whole pause + slack. A 0 count (the OLD
         * tick-freeze reading) or a feed-gap count (>>100k) would
         * falsify the model */
        check(r.underrun_ticks >= 30000 && r.underrun_ticks <=
                  (uint64_t)(1.25 * WIRE_RATE),
              "RED (audible): ~915 ms of DC-buzz underrun through the "
              "stall — the client never heard it");
        /* no truncation (fidel drain): the engine plays the FULL
         * pattern; its tail is chewed after close. The defect is the
         * done-claim EARLINESS, not data loss */
        check(r.emitted >= WIRE_2S - 240,
              "engine still plays the FULL pattern (no tail "
              "truncation) — the defect is the early over-claim, not "
              "data loss");
        /* done-claimed (drain no-op, pointer at cap) while the engine
         * still owes it a LARGE in-flight tail (socket queues +
         * ring): the 2.1 s queue ceiling absorbed the 1000 ms stall,
         * so close lands at the SAME push end as T1 (~1.6 s of wall,
         * == T1's 1.60 s) while the engine has chewed only ~1.0 s
         * of wire (it froze 1.0->2.0 s) — the over-claim is a FULL
         * second of audio at close (vs T1's ~0.4 s healthy tail):
         * the silence did not come back to the client, it came back
         * as claim-while-still-owing. Derived, not fitted: the claim
         * must be >= a healthy-T1-scale tail AND the under-served
         * amount (pause - ring chew ~ 0.83 s) must dominate it,
         * i.e. in [pause/2, pause + ring + slack]. The witness is
         * the live engine counter at the close instant (a torn read
         * is harmless: the check is a band, not an equality) */
        check(WIRE_2S - r.engine_emitted_at_close >= 24000 &&
              WIRE_2S - r.engine_emitted_at_close <= 72000,
              "RED: done-claimed while >=0.5 s of audio still in "
              "flight at close (the over-claim, on top of the buzz)");
        /* the at-close truth, printed like T1's pin */
        {
            uint64_t oc =
                (WIRE_2S > r.engine_emitted_at_close)
                    ? WIRE_2S - r.engine_emitted_at_close : 0;
            double oc_ms = oc ? (double)oc / WIRE_RATE * 1000.0 : 0.0;
            printf("    claim at drain-call : %lu client frames (file "
                   "end, cap)\n    engine truth @ close: %lu frames "
                   "chewed (%.0f ms of claimed audio still in flight "
                   "at close; chewed after the stall, plays after "
                   "close)\n       (over-claim at close: %lu wire "
                   "samples = %.0f ms; buzz: %lu ticks)\n",
                   (unsigned long)(WIRE_2S * W2C_NUM / W2C_DEN),
                   (unsigned long)(r.engine_emitted_at_close * W2C_NUM /
                                   W2C_DEN),
                   oc_ms, (unsigned long)oc, oc_ms,
                   (unsigned long)r.underrun_ticks);
        }
        /* --- v2: watchdog fires XRUN, run recovers --- */
        model.kind = 1;
        eng.pause_ns = 1000ull * 1000000ull; /* phase-1 lasts 1000 ms */
        g_phase = 1;
        alarm(1);
        run_client(2 * CLIENT_RATE, 30.0, &r);
        alarm(0);
        metrics_line("T6b(v2)", &r);
        check(r.xruns >= 1 && !r.failed && !r.wedged,
              "GREEN: v2 watchdog fires XRUN, run recovers");
        check(r.detect_s >= 1.5 && r.detect_s <= 2.6,
              "GREEN: detection within [stall+watchdog, stall+ring+watchdog+slack]");
        engine_stop(&eng);
    }

    printf("\n%s (%d failure%s)\n",
           nfail ? "SELF-TEST FAIL" : "SELF-TEST PASS", nfail,
           (nfail == 1) ? "" : "s");
    return nfail ? 1 : 0;
}