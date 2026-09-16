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
 *   - engine does not set socket buffers: we cap our SO_SNDBUF (measured
 *     prefill ceiling ~0.66 s at 48k mono — ~85 ms ring + bounded queue)
 *
 * What this plugin does, per open:
 *   - advertises RW_INTERLEAVED s16le mono/stereo, 11 common rates
 *   - prepare: picks a resampler (identity / up / down) from the rate
 *   - transfer: downmixes client frames to mono, linearly resamples to
 *     48k (exact streaming linear filter, ALSA pcm_rate_linear algorithm
 *     adapted to cross-chunk carry), blocking send()s; no internal ring
 *     (the socket IS the ring)
 *   - pointer: the engine exposes NO status channel, so consumption is
 *     ACCOUNTED against the wall clock (CLOCK_MONOTONIC): `mass` 48 kHz
 *     samples sent at time T drain by T + mass/48k (drain time), so
 *     pointer reports consumed = mass - in-flight in CLIENT frames.
 *     That lag behind appl is what makes __snd_pcm_hwsync actually wait;
 *     send() blocking on a full queue is the second backpressure belt
 *   - snd_pcm_wait() parks on POLLOUT of the engine socket (revents
 *     pass-through), so a "full" client sees it ready only when engine
 *     drains — natural run-to-start behaviour, zero busy-wait
 *   - engine death mid-job: send() fails -> dead -> -EPIPE; clients that
 *     snd_pcm_recover() reconnect on the next transfer (fresh job);
 *     pointer() reports -EIO so the state machine XRUNs instead of
 *     looping
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
#define HAT_SNDBUF         (64u * 1024)    /* our half of the prefill cap */

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

    /* pacing accounting (see hat_pointer): the engine's tick clock is
     * wall-locked, so consumed 48 kHz samples are a pure function of
     * wall time — mass sent drains by `drain`, then freezes until more
     * is sent (engine starves). pointer reports consumed in client
     * frames; the resampler carry keeps it <= frames ever accepted, so
     * hw never outruns appl and avail never goes negative. */
    uint64_t           mass;    /* 48 kHz samples handed to the socket */
    uint64_t           drain;   /* wall ns at which `mass` is consumed */
    snd_pcm_sframes_t  sent;    /* client frames CONSUMED (== pointer) */
    uint64_t           acc;     /* 48->client frame remainder, DIV scale */

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

/* connect a fresh engine job; caller must hold no pcm lock state
 * invariants (called from start/transfer callbacks, which run under
 * the pcm lock except drain — single-client deck, safe). */
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
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    /* bound our send queue: engine's ring (85 ms) + this is the total
     * prefill (measured ~0.66 s; the engine side is uncapped). */
    {
        int v = HAT_SNDBUF;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)) < 0)
            errno = 0; /* advisory only */
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

static snd_pcm_sframes_t hat_pointer(snd_pcm_ioplug_t *io)
{
    struct hat_state *st = io->private_data;
    uint64_t now, w, f, saved;

    /* monotonic; no boundary flag set, so the wrap branch is never
     * reachable and the state machine sees XRUN on -EIO. */
    if (st->dead)
        return -EIO;
    if (st->mass == 0)
        return st->sent; /* 0 until the first send */
    now = hat_now_ns();
    /* in flight = backlog still to be pulled at HAT_WIRE_RATE */
    if (now >= st->drain)
        w = st->mass;
    else {
        uint64_t rem = (st->drain - now) * HAT_WIRE_RATE
                      / 1000000000ull;
        w = st->mass - (rem > st->mass ? st->mass : rem);
    }
    /* 48 kHz samples -> client frames, DIV fixed point (monotonic: the
     * resampler carry guarantees this never exceeds frames accepted).
     * Divide before the DIV multiply so long sessions stay << u64 max. */
    f = ((w * (uint64_t)st->rate) / HAT_WIRE_RATE) * HAT_LIN_DIV;
    saved = (uint64_t)st->sent * HAT_LIN_DIV + st->acc;
    if (f > saved) {
        uint64_t d = f - saved;
        st->sent += (snd_pcm_sframes_t)(d >> HAT_LIN_SHIFT);
        st->acc = d & (HAT_LIN_DIV - 1);
    }
    return st->sent;
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
        const char *p;
        size_t left;

        hat_downmix((const int16_t *)base, st->channels, n_in, st->staging);

        if (st->dir == 0) {
            size_t i;
            for (i = 0; i < n_in; i++)
                st->out[i] = (int16_t)st->staging[i];
            n_out = n_in;
        } else {
            n_out = hat_resample(st, st->staging, n_in, dst);
        }

        p = (const char *)dst;
        left = n_out * sizeof(int16_t);
        while (left > 0) {
            ssize_t w = send(st->fd, p, left, MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                /* engine gone: report as recoverable underrun; the next
                 * transfer after snd_pcm_recover() reconnects (fresh job).
                 * partial chunk bytes may be in flight — fine, the job
                 * was dying anyway. */
                st->dead = 1;
                return -EPIPE;
            }
            p += w;
            left -= (size_t)w;
        }
        /* pacing: this chunk joins the engine's ring and is pulled out
         * at exactly HAT_WIRE_RATE. Cumulative: if the backlog still has
         * un-drained mass, append to its drain interval; if it already
         * drained (client slowed), anchor a fresh interval at `now`.
         * (Pointer math relies on CUMULATIVE arrival, not the last
         * chunk's — see hat_pointer.) */
        {
            uint64_t now = hat_now_ns();
            if (st->drain > now && st->mass > 0) {
                st->drain += (n_out * 1000000000ull) / HAT_WIRE_RATE;
            } else {
                st->drain = now + (n_out * 1000000000ull) / HAT_WIRE_RATE;
            }
        }
        st->mass += n_out;
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
    st->acc = 0;
    st->mass = 0;
    st->drain = hat_now_ns();
    st->dead = 0;
    return 0;
}

static int hat_drain(snd_pcm_ioplug_t *io)
{
    /* non-blocking by design: everything we care about is already sent;
     * framework drop() -> hat_stop() closes the socket, engine plays the
     * remainder of its ring then frees the pin. return 0 => drop. */
    (void)io;
    return 0;
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
    snd_output_printf(out, "hat: 48 kHz samples sent so far: %llu\n",
                      (unsigned long long)st->mass);
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