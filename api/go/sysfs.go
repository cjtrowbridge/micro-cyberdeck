package main

import (
	"os"
	"strconv"
	"strings"
	"syscall"
)

// Read-only sysfs/proc source access. Every reader degrades to a zero/nil
// result (never an error) so a missing sensor can never crash the API
// (plan 2026-09-19-23-23-36 §6).

func readTrim(path string) (string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(b)), nil
}

// readTempsLocked reads every discovered zone (type -> °C at 0.1 precision).
// Caller holds s.mu.
func (s *sampler) readTempsLocked() map[string]*float64 {
	temps := make(map[string]*float64, len(s.zoneTypes))
	for _, z := range s.zoneTypes {
		v, err := readTrim("/sys/class/thermal/thermal_zone" + strconv.Itoa(s.zoneIdx[z]) + "/temp")
		if err != nil || v == "" {
			temps[z] = nil
			continue
		}
		milli, err := strconv.ParseFloat(v, 64)
		if err != nil {
			temps[z] = nil
			continue
		}
		c := float64(int64(milli/100+0.5)) / 10.0 // 0.1 °C
		temps[z] = &c
	}
	return temps
}

func readMeminfo() memSample {
	var m memSample
	fields := make(map[string]uint64, 64)
	data, err := os.ReadFile("/proc/meminfo")
	if err != nil {
		return m
	}
	for _, line := range strings.Split(string(data), "\n") {
		kv := strings.SplitN(line, ":", 2)
		if len(kv) != 2 {
			continue
		}
		p := strings.Fields(kv[1])
		if len(p) == 0 {
			continue
		}
		if v, err := strconv.ParseUint(p[0], 10, 64); err == nil {
			fields[strings.TrimSpace(kv[0])] = v
		}
	}
	m.total = fields["MemTotal"]
	m.free = fields["MemFree"]
	m.available = fields["MemAvailable"]
	if m.available > m.total {
		m.available = m.total
	}
	m.used = m.total - m.available
	m.swapTotal = fields["SwapTotal"]
	m.swapFree = fields["SwapFree"]
	if m.swapFree > m.swapTotal {
		m.swapFree = m.swapTotal
	}
	return m
}

func readDisk(mount string) *diskSample {
	var st syscall.Statfs_t
	if err := syscall.Statfs(mount, &st); err != nil {
		return nil
	}
	bsize := uint64(st.Bsize)
	total := st.Blocks * bsize
	free := st.Bavail * bsize
	used := total - st.Bfree*bsize
	var pct float64
	if total > 0 {
		pct = float64(used) / float64(total) * 100
	}
	return &diskSample{Total: total, Used: used, Free: free, Pct: pct}
}

func readLoad() [3]*float64 {
	var out [3]*float64
	line, err := readTrim("/proc/loadavg")
	if err != nil {
		return out
	}
	p := strings.Fields(line)
	for i := 0; i < 3 && i < len(p); i++ {
		if f, err := strconv.ParseFloat(p[i], 64); err == nil {
			v := f
			out[i] = &v
		}
	}
	return out
}

// readFan reads the two "fan" readings of the deck (docs/hardware/fan.md):
// the duty from the hwmon entry matched BY NAME (`pwmfan`/pwm1 — hwmon
// numbering is not stable across boots) and the kernel cooling state of the
// cooling device of type `pwm-fan` (0-4).
func readFan() *fanState {
	f := &fanState{}
	fanHWMon := ""
	for i := 0; i < 64; i++ {
		p := "/sys/class/hwmon/hwmon" + strconv.Itoa(i)
		if t, err := readTrim(p + "/name"); err == nil && t == "pwmfan" {
			fanHWMon = p
			break
		}
	}
	if fanHWMon != "" {
		if v, err := readTrim(fanHWMon + "/pwm1"); err == nil {
			if n, err := strconv.Atoi(v); err == nil {
				f.PWM = &n
			}
		}
	}
	for i := 0; i < 64; i++ {
		p := "/sys/devices/virtual/thermal/cooling_device" + strconv.Itoa(i)
		t, err := readTrim(p + "/type")
		if err != nil {
			continue
		}
		if t == "pwm-fan" {
			if v, err2 := readTrim(p + "/cur_state"); err2 == nil {
				if n, err2 := strconv.Atoi(v); err2 == nil {
					f.State = &n
				}
			}
			break
		}
	}
	if f.PWM == nil && f.State == nil {
		return nil
	}
	return f
}

// readCPUJiffy sums the first /proc/stat `cpu` line into (idle, total)
// jiffies for the 2-sample utilization delta.
func readCPUJiffy() *cpuJiffy {
	data, err := os.ReadFile("/proc/stat")
	if err != nil {
		return nil
	}
	// NOTE: the first line IS the aggregate, but do NOT use
	// strings.SplitN(data, "\n", 1) here — with n=1 SplitN returns the
	// WHOLE file as a single element, so a HasPrefix(line, "cpu ") check
	// would always fail (this exact bug left cpu_util_pct permanently nil
	// until found on the first live converge, 2026-09-20).
	for _, line := range strings.Split(string(data), "\n") {
		if !strings.HasPrefix(line, "cpu ") {
			break
		}
		p := strings.Fields(line)
		if len(p) < 5 {
			return nil
		}
		var j cpuJiffy
		for i, v := range p[1:] {
			n, err := strconv.ParseUint(v, 10, 64)
			if err != nil {
				return nil
			}
			j.total += n
			if i == 3 { // idle field
				j.idle = n
			}
		}
		return &j
	}
	return nil
}
