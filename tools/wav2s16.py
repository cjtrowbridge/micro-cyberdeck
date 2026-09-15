#!/usr/bin/env python3
"""wav2s16.py — WAV -> raw s16le mono @ 48000 Hz (hat-sound's input format).

16-bit PCM WAV, mono or stereo, any sample rate -> raw little-endian s16
mono at 48 kHz: linear resample, (L+R)/2 stereo downmix. Stdlib-only
(wave + array), no daemon, no dependencies. The board has no ffmpeg/sox,
so this is the converter of record for hat-sound one-shot playback and
for setup.sh's tts-selftest verify row.

Usage:  wav2s16.py IN.wav OUT.s16
"""
import array
import sys
import wave

SAMPLE_RATE = 48000


def die(msg):
    print(f"wav2s16: {msg}", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        die("usage: wav2s16.py IN.wav OUT.s16")
    try:
        w = wave.open(sys.argv[1], "rb")
    except (EOFError, wave.Error) as e:
        die(f"cannot read {sys.argv[1]}: {e}")

    ch = w.getnchannels()
    sw = w.getsampwidth()
    rate = w.getframerate()
    if sw != 2:
        die(f"sample width {sw * 8} bits (need 16-bit PCM WAV)")
    if ch not in (1, 2):
        die(f"{ch} channels (need mono or stereo)")
    if rate <= 0:
        die(f"bad sample rate {rate}")
    try:
        data = w.readframes(w.getnframes())
    except (EOFError, wave.Error) as e:
        die(f"cannot read frames: {e}")
    finally:
        w.close()

    nframes = len(data) // (2 * ch)
    if nframes == 0:
        die("file contains no audio frames")
    frames = array.array("h")
    frames.frombytes(data[: nframes * 2 * ch])

    if ch == 2:  # (L+R)//2 downmix
        mono = array.array(
            "h", ((frames[i] + frames[i + 1]) // 2 for i in range(0, len(frames), 2))
        )
    else:
        mono = frames

    if rate == SAMPLE_RATE:
        out = mono
    else:
        src = len(mono)
        dst = int(round(src * SAMPLE_RATE / rate))
        step = rate / SAMPLE_RATE
        out = array.array("h")
        for i in range(dst):
            x = i * step
            j = int(x)
            if j >= src - 1:
                out.append(mono[src - 1])
            else:
                frac = x - j
                out.append(int(mono[j] * (1.0 - frac) + mono[j + 1] * frac))

    # Note: array('h') is native byte order; this board is little-endian
    # (aarch64), which is exactly the s16le hat-sound reads.
    with open(sys.argv[2], "wb") as f:
        f.write(out.tobytes())

    layout = "mono" if ch == 1 else "stereo"
    print(
        f"wav2s16: {nframes} frames @ {rate} Hz {layout}"
        f" -> {len(out)} samples @ {SAMPLE_RATE} Hz mono"
        f" ({len(out) / SAMPLE_RATE:.2f}s) -> {sys.argv[2]}"
    )


if __name__ == "__main__":
    main()