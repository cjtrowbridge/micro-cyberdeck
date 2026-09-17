/* libasound "hat" PCM plugin — the GamePi sound HAT to userland.
 *
 * Exposes a real playback device that behaves like a normal ALSA PCM:
 *
 *   pcm.hat { type hat; }                     # or: hat_path "/run/other.sock"
 *   pcm.!default { type plug; slave.pcm "hat"; }   # make it the default out
 *
 * and then `aplay -D hat file.wav` works for s16 mono/stereo 8k..192k.
 *
 * Model (frozen wire contract with tools/hat-sound.c, engine "hat-sound"):
 *   - engine = single-GPIO 1-bit sigma-delta, 48 kHz mono s16le byte stream
 *     on AF_UNIX SOCK_STREAM /run/gamepi-sound.sock (mode 0666, root, RT)
 *   - one connection = one job: engine resets integrator on connect,
 *     plays ring tail after our close (clean EOF = drain), logs stats
 *   - engine does not set socket buffers: we cap our SO_SNDBUF (the
 *     prefill ceiling; see the transport-budget note below) and run it
 *     NONBLOCKING, so a dead/wedged engine never blocks a client call
 *
 * What this plugin does, per open (v2):
 *   - advertises RW_INTERLEAVED s16le mono/stereo, 11 common rates
 *   - prepare: picks a resampler (identity / up / down) from the rate and
 *     anchors the consumption clock at the job start
 *   - transfer: downmixes client frames to mono, linearly resamples to
 *     48k (exact streaming linear filter, ALSA pcm_rate_linear algorithm
 *     adapted to cross-chunk carry), then send()s a chunk NONBLOCKING
 *     under a bounded poll() watchdog; no internal ring (the socket IS the
 *     ring)
 *   - pointer: a GATED SCHEDULE — `consumed` 48 kHz samples advance at
 *     exactly HAT_WIRE_RATE, but ONLY while there is in-flight backlog
 *     (consumed < sent); each resumption re-anchors at now (stall-aware).
 *     The invariant consumed <= sent means the pointer can NEVER outrun
 *     bytes actually handed to the engine — the fix for the v1 defect,
 *     where `mass`/`drain` was a pure send-side clock, so `available`
 *     went negative the instant `sent` froze, the engine starved, and a
 *     stalled feeder rode through for minutes (tools/hat_pace_selftest.c,
 *     cases T1/T3/T6). Reported in CLIENT frames.
 *   - snd_pcm_wait() parks on POLLOUT of the engine socket (revents
 *     pass-through): a "full" client sees it ready only when the engine
 *     drains the queue — natural run-to-start, zero busy-wait
 *   - STALL/DEATH DETECTION (the offline self-test's T3/T4/T6b, in-product):
 *       * transport dead: send() -> EPIPE/ECONNRESET/ENOTCONN, or POLLERR/
 *         POLLHUP -> dead -> -EIO; a client that snd_pcm_recover()s
 *         reconnects on the next transfer (fresh job)
 *       * send wedged (nonblocking, no progress) past WATCHDOG -> -EIO
 *       * pointer frozen >= WATCHDOG while a schedule is still PENDING
 *         (sent ahead of consumed) -> dead -> -EIO. Health check that
 *         cannot false-fire on a healthy run: consumed advances while
 *         sent > consumed, and at buffer end there is no pending
 *         schedule — the check stays silent through the ~85 ms ring tail.
 *
 * The FROZEN v3.6 engine exposes NO delivered-bytes counter channel, so
 * the consumption evidence bound is `sent` (handed to the kernel): a
 * stall SHORTER than the socket budget is absorbed by the queue and only
 * surfaces once the queue drains and consumed freezes — see the offline
 * self-test's evidence-cap note and the journal. If the engine ever gains
 * a delivered-bytes counter, cap consumed by it (stronger bound) instead.
 *
 * Transport budget (mirrors the offline self-test's pinned v2 values):
 *   - we cap SO_SNDBUF at HAT_SNDBUF (advisory: the 16 KiB request reads
 *     back ~2x, and the kernel feeds the engine's default rcvbuf).
 *     Worst case send-ahead = socket + ring (bounded sub-second) at 48k
 *     mono. This small prefill is deliberate: it bounds how much a
 *     stalled feeder can over-claim before the leaky bucket
 *     (consumed <= sent) exposes it at the pointer, and keeps the
 *     client's avail from going deeply negative.
 *   - HAT_WATCHDOG_NS (600 ms) mirrors the self-test's WATCHDOG_MS (600):
 *     any no-progress or send-side stall longer than this is a fault,
 *     not a slow engine — the whole point of v2.
 *
 * Build (board or host):
 *   cc -O2 -Wall -Wextra -fPIC -DPIC -shared \
 *      -o libasound_module_pcm_hat.so hat_alsa_plugin.c -lasound
 * (-DPIC is load-bearing: it selects the PIC branch of
 *  SND_DLSYM_BUILD_VERSION in global.h; without it the static branch
 *  emits a constructor referencing libasound's non-PIC-only global
 *  snd_dlsym_start, which dlopen cannot resolve against a shared
 *  libasound.so — the module would fail to load at aplay time.)
 * Install to /usr/lib/aarch64-linux-gnu/alsa-lib/ (dlopen'd per open).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <alsa/pcm_external.h>

#define HAT_SOCKET_DEFAULT "/run/gamepi-sound.sock"
#define HAT_WIRE_RATE      48000u          /* engine fixed rate, Hz */
#define HAT_SAMPBYTES      2u              /* 16-bit mono wire sample */

/* transport budget — the pinned v2 values from the offline self-test
 * (tools/hat_pace_selftest.c): a SMALL send-ahead so a stalled feeder
 * over-claims little before the leaky bucket (consumed <= sent) exposes
 * it, and a 600 ms WATCHDOG that turns any no-progress stall into a
 * recoverable fault instead of a silent ride-through (the v1 sin). */
#define HAT_SNDBUF        (16u * 1024)     /* SO_SNDBUF (advisory, ~2x) */
#define HAT_WATCHDOG_NS   (600ull * 1000000ull) /* stalled-engine bound */
#define HAT_RING_NS       (180ull * 1000000ull) /* engine ring drain margin */
#define HAT_NSC2S         1000000000ull    /* wall ns -> 48 kHz samples */

/* resampling fixed point (same scale as ALSA's linear rate plugin) */
#define HAT_LIN_SHIFT 19
#define HAT_LIN_DIV   (1u << HAT_LIN_SHIFT)

/* scratch limits: one transfer chunk's work must fit these.
 * n_out < 1 + n_in*48000/rate <= 1 + HAT_STAGING (n_in is capped in
 * hat_chunk) — the +16 margin covers that off-by-one with headroom. */
#define HAT_STAGING 32768u                 /* mono input samples,  s32 */
#define HAT_MAXOUT  (HAT_STAGING + 16u)    /* 48k output samples, s16 */

struct hat_state {
    char              *path;      /* strdup'd socket path */
    int                fd;        /* -1 = closed */
    int                dead;      /* transport failed -> reconnect next call */

    unsigned int       rate;      /* client rate negotiated (Hz) */
    unsigned int       channels;  /* client channels 1|2 */
    unsigned int       dir;       /* 0 = identity, 1 = up, 2 = down */
    unsigned int       pitch;     /* 48k ticks per client tick, HAT_LIN_DIV scale */

    /* streaming resampler carry (monotonic phase in [0, HAT_LIN_DIV)) */
    unsigned int       pos;
    int32_t            prev;      /* last input sample (int32 for weight math) */

    /* v2 pacing: a GATED leaky bucket. `sent` = wire samples handed to
     * the engine (acceptance boundary); `consumed` = wire samples the
     * engine has actually chewed. consumed advances at HAT_WIRE_RATE but
     * ONLY while backlog is in flight (consumed < sent); on each resume
     * the clock re-anchors at `now` (hat_now_ns), so a wall gap while
     * the socket is empty contributes nothing. Invariant: consumed
     * <= sent, so the pointer can never outrun bytes handed to the
     * engine (v1's `mass`/`drain` had no such ceiling — the defect that
     * let a stalled feeder over-claim minutes). Stall detection (see
     * hat_pointer): while sent > consumed (a schedule is PENDING), a
     * frozen consumed for HAT_WATCHDOG_NS is a fault -> -EIO. At buffer
     * end consumed == sent and there is no pending schedule, so the
     * check stays silent through the ~85 ms ring tail. */
    uint64_t           sent;        /* wire SAMPLES handed to the engine */
    uint64_t           consumed;    /* wire SAMPLES chewed (leaky bucket) */
    uint64_t           anchor_ns;   /* wall ns where consumed's clock runs */
    snd_pcm_sframes_t  frames_sent; /* client frames consumed (== pointer) */
    uint64_t           acc;         /* 48->client frame remainder, DIV scale */
    uint64_t           ptr_val;     /* last observed consumed (stall detector) */
    uint64_t           ptr_ns;      /* wall ns `ptr_val` was first observed */
    int                ptr_have;    /* ptr_val/ptr_ns valid */

    int32_t            *staging;  /* HAT_STAGING */
    int16_t            *out;      /* HAT_MAXOUT */

    snd_pcm_ioplug_t   *plug;     /* heap struct we own; closed in hat_close */
};

/* ------------------------------------------------------------------ */
/* resampler: mono s32 staging -> 48k s16 out, exact across chunks     */
/* ------------------------------------------------------------------ */

/* one call, both directions; pitch = round(48000 * DIV / rate) so
 * pitch/DIV = 48000/rate: pitch> DIV upsamples, pitch< DIV downsamples.
 * Every emitted sample is the 2-point linear interpolation at phase pos
 * between the previous and current input; phase never wraps, so rate
 * matches to < 1 ppm (no ALSA per-period prime/reset needed). */
static size_t hat_resample(struct hat_state *st,
                           const int32_t *src, size_t n_in, int16_t *out)
{
    uint64_t pos = st->pos;
    const uint32_t pitch = st->pitch;
    int32_t old = st->prev;
    size_t i, n = 0;

    for (i = 0; i < n_in; i++) {
        int32_t cur = src[i];
        pos += pitch;
        while (pos >= HAT_LIN_DIV) {
            pos -= HAT_LIN_DIV;
            uint32_t w = (uint32_t)(((pos << 16) / HAT_LIN_DIV) & 0xFFFFu);
            int64_t v = (int64_t)old * (int64_t)(0x10000 - (int32_t)w) +
                        (int64_t)cur * (int64_t)w;
            out[n++] = (int16_t)(v >> 16);
        }
        old = cur;
    }
    st->pos = (unsigned int)pos;
    st->prev = old;
    return n;
}

/* downmix interleaved s16 client frames to mono s32 (round half away) */
static void hat_downmix(const int16_t *f, unsigned int ch, size_t n,
                        int32_t *mono)
{
    size_t i;
    if (ch == 1) {
        for (i = 0; i < n; i++)
            mono[i] = f[i];
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        unsigned int c;
        for (c = 0; c < ch; c++)
            s += f[i * ch + c];
        mono[i] = (s + (int32_t)ch / 2) / ch;
    }
}

/* ------------------------------------------------------------------ */
/* socket job handling                                                 */
/* ------------------------------------------------------------------ */

static void hat_disconnect(struct hat_state *st)
{
    if (st->fd >= 0)
        close(st->fd);
    st->fd = -1;
}

static uint64_t hat_now_ns(void);   /* used by hat_connect below it */

/* 10 ms pause for the connect retry (nanosleep: POSIX under our
 * _POSIX_C_SOURCE, no usleep portability footgun). */
static void hat_sleep_ms(unsigned int ms)
{
    struct timespec d = { 0, (long)ms * 1000000L };
    (void)nanosleep(&d, NULL);
}

/* connect a fresh engine job (v2). Like the offline self-test's
 * model_connect (its v2 branch): a single client gives the engine up to
 * ~2 s to become ready — tolerating, say, a systemd restart right after
 * an XRUN before we fail. (v1 did ONE attempt; the retry is part of the
 * v2 model the self-test pins.) The socket runs NONBLOCKING with our
 * half of the transport budget (HAT_SNDBUF), so a dead/wedged engine
 * never blocks a client call — hat_transfer's watchdog is the backstop.
 * Caller must hold no pcm-lock invariants (called from the
 * start/transfer callbacks, which run under the pcm lock except drain —
 * single-client deck, safe). */
static int hat_connect(struct hat_state *st)
{
    struct sockaddr_un a;
    int fd;

    /* idempotent: transfer() connects lazily before the start() callback
     * runs, so hat_start() must not open a second job on this socket */
    if (st->fd >= 0 && !st->dead)
        return 0;

    st->dead = 0;
    hat_disconnect(st);

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    if (snprintf(a.sun_path, sizeof(a.sun_path), "%s", st->path)
            >= (int)sizeof(a.sun_path)) {
        close(fd);
        return -ENAMETOOLONG;
    }
    {
        uint64_t deadline = hat_now_ns() + 2000000000ull; /* ~2 s */
        for (;;) {
            if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0)
                break;
            close(fd);
            if (hat_now_ns() >= deadline)
                return -EIO; /* engine not ready within the budget */
            hat_sleep_ms(10);
            fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd < 0)
                return -errno;
        }
    }
    /* our half of the transport budget: caps how far ahead a stalled
     * feeder can run before consumed (<= sent) exposes it (see header). */
    {
        int v = HAT_SNDBUF;
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v));
    }
    /* NONBLOCKING: send() -> -EAGAIN on a full queue instead of blocking
     * forever on a dead engine; the watchdog in hat_transfer bounds it. */
    {
        int fl = fcntl(fd, F_GETFL, 0);
        (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    st->fd = fd;
    return 0;
}

/* ------------------------------------------------------------------ */
/* ioplug callbacks                                                    */
/* ------------------------------------------------------------------ */

static int hat_start(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;
    int err;

    err = hat_connect(st);
    if (err < 0) {
        st->dead = 1;
        return err;
    }
    io->poll_fd = st->fd;
    io->poll_events = POLLOUT | POLLERR | POLLHUP;
    snd_pcm_ioplug_reinit_status(io);
    return 0;
}

static int hat_stop(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;

    hat_disconnect(st);
    io->poll_fd = -1;
    io->poll_events = 0;
    snd_pcm_ioplug_reinit_status(io);
    return 0;
}

static uint64_t hat_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* the gated leaky bucket: consumed = samples chewed. The engine pulls
 * the socket at HAT_WIRE_RATE, so consumed advances at that rate ONLY
 * while backlog is in flight (consumed < sent); on each resume the clock
 * re-anchors at `now`, so a wall gap while the socket is empty adds no
 * consumption. Invariant: consumed <= sent (the pointer can never outrun
 * what was handed to the engine). Called on every pointer()/drain() tick. */
static void hat_consume_advance(struct hat_state *st, uint64_t now)
{
    if (st->sent > st->consumed) {
        uint64_t el = now > st->anchor_ns ? now - st->anchor_ns : 0;
        /* split multiply: (el % NSC2S) * RATE < 1e9 * 48000, so the
         * product cannot overflow u64 even for absurd el; the whole-second
         * term overflows only at ~24 years of continuous pending. */
        uint64_t gained = (el / HAT_NSC2S) * HAT_WIRE_RATE +
                          ((el % HAT_NSC2S) * HAT_WIRE_RATE) / HAT_NSC2S;
        if (gained > st->sent - st->consumed)
            gained = st->sent - st->consumed;
        st->consumed += gained;
        st->anchor_ns = now;
    }
    /* frozen while PENDING = a fault. A schedule is pending iff the
     * engine still has bytes to chew (sent ahead of consumed). Once the
     * schedule is fully chewed there is no pending, so the check cannot
     * fire through the healthy ~85 ms ring tail. This mirrors the
     * self-test's v2_front_stalled (front frozen with work pending). */
    if (st->sent > st->consumed) {
        if (!st->ptr_have || st->consumed != st->ptr_val) {
            st->ptr_val = st->consumed;
            st->ptr_ns = now;
            st->ptr_have = 1;
        } else if (now - st->ptr_ns >= HAT_WATCHDOG_NS) {
            st->dead = 1;
        }
    } else {
        st->ptr_have = 0; /* fully chewed: clear the stall clock */
    }
}

/* pointer = consumed (samples) -> client frames, DIV fixed point.
 * Monotone by construction (leaky bucket only advances); 0 until bytes
 * are handed to the engine (sent > 0). Dead transport or a frozen front
 * -> -EIO, which the state machine turns into a recoverable XRUN
 * instead of looping (the v1 "pointer is the clock" defect, fixed). */
static snd_pcm_sframes_t hat_pointer(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;
    uint64_t now, f;

    if (st->dead)
        return -EIO;
    if (st->sent == 0)
        return 0; /* nothing handed to the engine yet */

    now = hat_now_ns();
    hat_consume_advance(st, now);
    if (st->dead)
        return -EIO; /* front froze while a schedule was pending */

    /* wire samples -> client frames, DIV scale; advance the reported
     * counter monotonically (divide before the DIV multiply so long
     * sessions stay well under u64 max). */
    f = ((st->consumed * (uint64_t)st->rate) / HAT_WIRE_RATE) * HAT_LIN_DIV;
    {
        uint64_t saved =
            (uint64_t)st->frames_sent * HAT_LIN_DIV + st->acc;
        if (f > saved) {
            uint64_t d = f - saved;
            st->frames_sent += (snd_pcm_sframes_t)(d >> HAT_LIN_SHIFT);
            st->acc = d & (HAT_LIN_DIV - 1);
        }
    }
    return st->frames_sent;
}

static size_t hat_chunk(const struct hat_state *st)
{
    if (st->dir == 1) { /* upsample: cap input so output fits HAT_MAXOUT */
        uint64_t c = (uint64_t)HAT_STAGING * st->rate / HAT_WIRE_RATE;
        if (c < 1)
            c = 1;
        if (c > HAT_STAGING)
            c = HAT_STAGING;
        return (size_t)c;
    }
    return HAT_STAGING;
}

/* The streaming resampler carries phase (pos) and prev across chunk
 * calls. The v2 transport escapes mid-transfer to -EPIPE (recoverable
 * by aplay/snd_pcm_recover) on a dead or wedged transport — and the
 * self-test's -EAGAIN soft-pause is deliberately NOT used in-product,
 * because it is safe only for its synthetic-zero content: the resampler
 * carry must stay contiguous with the bytes the client has handed over.
 * A failed mid-chunk attempt must therefore not leave the carry
 * advanced. Roll the carry back to just before this chunk: phase moves
 * back by n_in*pitch (mod HAT_LIN_DIV); prev = the last consumed MONO
 * input sample (mono domain, so 1-ch and 2-ch clients are both exact;
 * still valid because hat_downmix wrote this chunk's staging first).
 * frames_sent/acc need no rollback: they advance only via `consumed`,
 * which is updated solely on whole accepted chunks (so frames_sent
 * remains <= frames of any completed chunk). On the resulting XRUN,
 * prepare() -> hat_prepare() re-initialises the carry anyway; the
 * restore keeps in-flight state honest in the meantime. */
static void hat_restore_carry(struct hat_state *st, snd_pcm_uframes_t n_in)
{
    uint64_t back = (uint64_t)n_in * st->pitch % HAT_LIN_DIV;

    /* pos advanced by n_in*pitch (mod DIV); roll it back exactly. */
    st->pos = (unsigned int)(st->pos + HAT_LIN_DIV - back) % HAT_LIN_DIV;
    /* prev = the last CONSUMED MONO input sample. st->staging still holds
     * this chunk's downmix at failure time (mono domain: correct for
     * 1-ch AND 2-ch clients). */
    if (n_in > 0)
        st->prev = st->staging[n_in - 1];
}

/* nonblocking send of one resampled chunk (`nsamples` wire s16 in `out`)
 * to the engine socket, bounded by the watchdog. Returns 0 on a complete
 * send (the caller then advances sent/anchor), or -EIO on a dead/wedged
 * transport (EPIPE/ECONNRESET/ENOTCONN, a POLLERR/POLLHUP, or no progress
 * across the full watchdog). (Internal convention: -EIO; the caller maps
 * it to -EPIPE for the client — snd_pcm_recover() recovers -EPIPE but not
 * -EIO.) The socket is O_NONBLOCK, so send() returns
 * -EAGAIN (never blocks) until the engine drains the queue; the poll
 * bounds how long we wait before declaring the engine wedged. A failed
 * path may leave a partial prefix already in the kernel — fine: the job
 * is dying and prepare() resets the carry on the next XRUN. */
static int hat_send_chunk(struct hat_state *st, const int16_t *out,
                          size_t nsamples)
{
    const uint8_t *p = (const uint8_t *)out;
    size_t left = nsamples * HAT_SAMPBYTES;
    uint64_t block_start = 0;

    while (left > 0) {
        ssize_t w = send(st->fd, p, left, MSG_NOSIGNAL);
        if (w >= 0) {
            p += (size_t)w;
            left -= (size_t)w;
            block_start = 0;            /* resumed: reset the block clock */
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            uint64_t now;
            if (!block_start) {
                block_start = hat_now_ns();
                continue;
            }
            now = hat_now_ns();
            if (now - block_start >= HAT_WATCHDOG_NS)
                return -EIO;             /* wedged: no progress */
            struct pollfd pf = { .fd = st->fd,
                                 .events = POLLOUT | POLLERR | POLLHUP };
            int pr = poll(&pf, 1,
                          (int)(((HAT_WATCHDOG_NS - (now - block_start)) /
                                 1000000ull) + 1));
            if (pr < 0 && errno != EINTR)
                return -EIO;
            if (pr > 0 && (pf.revents & (POLLERR | POLLHUP)))
                return -EIO;             /* dead socket; aplay -> XRUN */
            continue;                    /* woke (or timed out): retry */
        }
        return -EIO;                     /* ECONNRESET etc: transport dead */
    }
    return 0;
}

static snd_pcm_sframes_t hat_transfer(snd_pcm_ioplug_t *io,
                                      const snd_pcm_channel_area_t *areas,
                                      snd_pcm_uframes_t offset,
                                      snd_pcm_uframes_t size)
{
    struct hat_state *st = io->private_data;
    const uint8_t *base;
    snd_pcm_uframes_t done = 0;

    base = (const uint8_t *)areas[0].addr + areas[0].first / 8 +
           offset * st->channels * 2;

    while (done < size) {
        /* The framework runs the first transfer while the stream is still
         * PREPARED and only calls start() after it — connect lazily. */
        if (st->fd < 0) {
            int c = hat_connect(st);
            if (c < 0)
                return done ? (snd_pcm_sframes_t)done : c;
            io->poll_fd = st->fd;
            io->poll_events = POLLOUT | POLLERR | POLLHUP;
            snd_pcm_ioplug_reinit_status(io);
        }

        size_t n_in = (size - done) < hat_chunk(st) ? (size_t)(size - done)
                                                    : hat_chunk(st);
        int16_t *dst = st->out;
        size_t n_out;

        hat_downmix((const int16_t *)base, st->channels, n_in, st->staging);

        if (st->dir == 0) {
            size_t i;
            for (i = 0; i < n_in; i++)
                st->out[i] = (int16_t)st->staging[i];
            n_out = n_in;
        } else {
            n_out = hat_resample(st, st->staging, n_in, dst);
        }

        if (hat_send_chunk(st, dst, n_out) < 0) {
            /* transport dead or wedged past the watchdog: close the
             * socket (the dying job ends at engine EOF and the next
             * transfer gets a fresh connection = fresh engine job),
             * roll the resampler carry back to before this chunk, and
             * die. The error is -EPIPE (NOT -EIO): snd_pcm_recover()
             * only recovers -EPIPE/-ESTRPIPE/-EINTR; aplay would exit
             * on -EIO. aplay sees -EPIPE -> snd_pcm_recover() ->
             * snd_pcm_prepare() (which resets the job anchor) -> the
             * next transfer reconnects. The POINTER side of -EIO
             * (hat_pointer -> dead) is what drives the framework XRUN:
             * hwsync error -> avail EPIPE -> XRUN -> -EPIPE to aplay. */
            hat_disconnect(st);
            hat_restore_carry(st, n_in);
            st->dead = 1;
            return -EPIPE;
        }

/* pacing: this chunk joins the engine's ring and is pulled out at
         * exactly HAT_WIRE_RATE back-to-back behind whatever is already
         * queued (self-test v2_chewed_wire: "start = entry > front_end ?
         * entry : front_end"). It is accepted by the kernel at `now`, so
         * it extends the send-side boundary (`sent`); the bucket clock
         * re-anchors at `now` ONLY when the engine is already fully
         * chewed (the send queue was dry) — i.e. this chunk's chewing
         * starts now. After a starve gap the engine catches up to the
         * ring in the gap, so an unmodified anchor would credit the
         * gap's worth of chewing to this chunk (the v1 over-claim sin);
         * re-anchoring on the dry condition removes the slack — mirrors
         * the self-test's entry-vs-front-end. hat_consume_advance keeps
         * consumed <= sent. */
        {
            uint64_t now = hat_now_ns();
            if (st->consumed == st->sent)
                st->anchor_ns = now;
            st->sent += n_out;
        }
        done += n_in;
        base += n_in * st->channels * 2;
    }
    return (snd_pcm_sframes_t)done;
}

static int hat_prepare(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;

    /* framework filled io->* from hw_params before this callback */
    st->rate = io->rate;
    st->channels = io->channels;
    st->dir = (io->rate == HAT_WIRE_RATE) ? 0
            : (io->rate <  HAT_WIRE_RATE) ? 1 : 2;
    st->pitch = (unsigned int)(((uint64_t)HAT_WIRE_RATE << HAT_LIN_SHIFT) +
                               io->rate / 2) / io->rate;
    st->pos = 0;
    st->prev = 0;
    st->sent = 0;
    st->consumed = 0;
    st->anchor_ns = hat_now_ns();
    st->frames_sent = 0;
    st->acc = 0;
    st->dead = 0;
    /*
     * The framework's prepare() resets last_hw (= our virtual hw.ptr)
     * to 0 (pcm_ioplug.c:167 snd_pcm_ioplug_reset) BEFORE calling this
     * callback — spec line 44: "prepare resets last_hw via reset". So
     * the pointer MUST come back from scratch for the new job: the
     * stall clock (ptr_val/ptr_ns/ptr_have) rides the same job boundary
     * as the chew state (self-test model_reset: "a prepare-then-reconnect
     * (XRUN recovery) MUST start with a clean front"), else a recovery
     * into a deep stall could false-fire the watchdog on resume.
     */
    st->ptr_val = 0;
    st->ptr_ns = 0;
    st->ptr_have = 0;
    return 0;
}

static int hat_drain(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;

    /* Framework (pinned 1.2.14, pcm_ioplug.c:549-553) RELEASES the pcm
     * lock before calling here, so sleeping is safe. Wait until the
     * leaky bucket says the engine has chewed every accepted chunk
     * (consumed == sent), then allow the ring drain margin: the ring
     * holds <= RING_MAX = 8192 B of wire at 48000 samples/s = exactly
     * HAT_RING_NS (180 ms). Mirrors the self-test's done_condition --
     * aplay's measured drain then approximates TRUE pin silence, which
     * is what the live gate (a) checks. A live `amixer`/`top` style
     * stall in the engine shows up at the WATCHDOG (dead -> -EIO, the
     * framework then drops + stops, engine still plays its small ring
     * tail after our close -- no truncation on a healthy engine). */
    while (1) {
        hat_consume_advance(st, hat_now_ns());
        if (st->dead) {
            /* the engine stopped chewing mid-drain (stall past the
             * watchdog): close the job and report -EIO -- which this
             * framework turns into a drop while DRAINING
             * (pcm_ioplug.c:497 snd_pcm_ioplug_drop), so hat_stop()
             * frees the socket and the client goes through the normal
             * XRUN recovery. We cannot keep waiting: the engine is not
             * going to chew. */
            hat_disconnect(st);
            return -EIO;
        }
        if (st->sent == 0)
            return 0;                    /* never started: nothing to chew */
        if (st->consumed < st->sent) {
            hat_sleep_ms(10);            /* engine still chewing */
            continue;
        }
        hat_sleep_ms((unsigned int)(HAT_RING_NS / 1000000ull));
        return 0;                        /* chewed + ring margin: drop */
    }
}

static void hat_dump(snd_pcm_ioplug_t *io, snd_output_t *out)
{
    const struct hat_state *st = io->private_data;

    snd_output_printf(out, "hat: %s\n", st->path);
    snd_output_printf(out,
                      "hat: client %lu Hz x %u ch -> 48 kHz mono s16le "
                      "(%s)\n",
                      (unsigned long)st->rate, st->channels,
                      st->dir == 0 ? "identity" :
                      st->dir == 1 ? "upsample" : "downsample");
    snd_output_printf(out, "hat: 48 kHz samples -> sent %llu (consumed "
                      "%llu, in flight %llu)\n",
                      (unsigned long long)st->sent,
                      (unsigned long long)st->consumed,
                      (unsigned long long)(st->sent - st->consumed));
    snd_output_printf(out, "hat: frames reported to client: %ld\n",
                      (long)(st->frames_sent > 0 ? st->frames_sent : 0));
}

static int hat_close(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;

    /* ioplug_delete = pcm close op = this callback, then framework
     * frees only its own priv — we own everything else. */
    hat_disconnect(st);
    free(st->staging);
    free(st->out);
    free(st->path);
    free(st);
    free((char *)io->name);
    free(io);
    return 0;
}

static const snd_pcm_ioplug_callback_t hat_cb = {
    .start    = hat_start,
    .stop     = hat_stop,
    .pointer  = hat_pointer,
    .transfer = hat_transfer,
    .close    = hat_close,
    .hw_params = NULL,
    .hw_free  = NULL,
    .sw_params = NULL,
    .prepare  = hat_prepare,
    .drain    = hat_drain,
    .pause    = NULL,
    .resume   = NULL,
    .poll_descriptors_count = NULL,
    .poll_descriptors       = NULL,
    .poll_revents           = NULL,
    .dump     = hat_dump,
    .delay    = NULL,
    .query_chmaps = NULL,
    .get_chmap    = NULL,
    .set_chmap    = NULL,
};

/* ------------------------------------------------------------------ */
/* plugin entry                                                        */
/* ------------------------------------------------------------------ */

int SND_PCM_PLUGIN_ENTRY(hat)(snd_pcm_t **pcmp, const char *name,
                              snd_config_t *root, snd_config_t *conf,
                              snd_pcm_stream_t stream, int mode)
{
    static const unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED };
    static const unsigned int format_list[] = { SND_PCM_FORMAT_S16_LE };
    static const unsigned int channel_list[] = { 1, 2 };
    static const unsigned int rate_list[] = {
        8000u, 11025u, 16000u, 22050u, 32000u, 44100u,
        48000u, 88200u, 96000u, 176400u, 192000u
    };
    struct hat_state *st;
    snd_pcm_ioplug_t *plug = NULL;
    char *path = NULL;
    int err;

    (void)root;
    if (stream != SND_PCM_STREAM_PLAYBACK)
        return -EINVAL;

    /* optional: pcm.hat { type hat; hat_path "/some/other.sock"; } */
    if (conf) {
        snd_config_t *c;
        const char *s;
        if (snd_config_searchv(conf, &c, "hat_path", NULL) == 0 &&
            snd_config_get_type(c) == SND_CONFIG_TYPE_STRING &&
            snd_config_get_string(c, &s) == 0 && s)
            path = strdup(s);
    }
    if (!path)
        path = strdup(HAT_SOCKET_DEFAULT);

    st = calloc(1, sizeof(*st));
    if (!path || !st) {
        err = -ENOMEM;
        goto err_alloc;
    }
    st->fd = -1;
    st->path = path;
    path = NULL;
    st->staging = calloc(HAT_STAGING, sizeof(int32_t));
    st->out = calloc(HAT_MAXOUT, sizeof(int16_t));
    if (!st->staging || !st->out) {
        err = -ENOMEM;
        goto err_alloc;
    }

    /* heap-owned public struct: ioplug_create sets data = this pointer and
     * the callbacks find us via private_data; hat_close frees all of it. */
    plug = calloc(1, sizeof(*plug));
    if (!plug) {
        err = -ENOMEM;
        goto err_alloc;
    }
    st->plug = plug;
    plug->version = SND_PCM_IOPLUG_VERSION;
    plug->name = strdup(name);
    if (!plug->name) {
        err = -ENOMEM;
        goto err_alloc;
    }
    plug->flags = SND_PCM_IOPLUG_FLAG_LISTED;
    plug->mmap_rw = 0;
    plug->poll_fd = -1;
    plug->poll_events = 0;
    plug->callback = &hat_cb;
    plug->private_data = st;

    err = snd_pcm_ioplug_create(plug, name, stream, mode);
    if (err < 0)
        goto err_alloc;

    err = snd_pcm_ioplug_set_param_list(plug, SND_PCM_IOPLUG_HW_ACCESS,
                                        1, access_list);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_list(plug, SND_PCM_IOPLUG_HW_FORMAT,
                                        1, format_list);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_list(plug, SND_PCM_IOPLUG_HW_CHANNELS,
                                        2, channel_list);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_list(plug, SND_PCM_IOPLUG_HW_RATE,
                                        11, rate_list);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_minmax(plug, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
                                          64u, 32768u);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_minmax(plug, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
                                          4096u, 65536u);
    if (err < 0)
        goto err_after_create;
    err = snd_pcm_ioplug_set_param_minmax(plug, SND_PCM_IOPLUG_HW_PERIODS,
                                          2u, 64u);
    if (err < 0)
        goto err_after_create;

    *pcmp = plug->pcm;
    return 0;

err_after_create:
    /* delete = close op = hat_close, which frees st + scratch + path +
     * plug itself. nothing else to free here. */
    snd_pcm_ioplug_delete(plug);
    return err;

err_alloc:
    if (plug) {
        free((char *)plug->name);
        free(plug);
    }
    if (st) {
        free(st->staging);
        free(st->out);
        free(st->path);
        free(st);
    }
    free(path);
    return err;
}

SND_PCM_PLUGIN_SYMBOL(hat);