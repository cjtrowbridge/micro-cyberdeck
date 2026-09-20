package main

import (
	"context"
	"sort"
	"strconv"
	"sync"
	"time"
)

// Sampling model (plan 2026-09-19-23-23-36 §4/§5): one sample every 5 s into
// a 720-slot ring = one hour. Nothing is persisted — after a reboot the
// chart fills back in over the next hour.
const (
	tickInterval = 5 * time.Second
	ringSize     = 720
)

type sample struct {
	ts      time.Time
	temps   map[string]*float64 // zone type -> °C (0.1 precision); nil = sensor unreadable
	mem     memSample
	disk    *diskSample // nil when statfs failed
	cpuUtil *float64    // nil on the first tick (no prior jiffies for a delta)
	load    [3]*float64 // load 1/5/15; nil entry = parse failed
	fan     *fanState   // nil when neither fan source was readable
}

type memSample struct {
	total, free, available, used uint64 // kB (meminfo)
	swapTotal, swapFree          uint64 // kB
}

type diskSample struct {
	Total uint64  `json:"total_bytes"`
	Used  uint64  `json:"used_bytes"`
	Free  uint64  `json:"free_bytes"`
	Pct   float64 `json:"pct"` // used/total*100
}

type fanState struct {
	PWM   *int `json:"pwm"`   // duty 0-255 (hwmon `pwmfan`/pwm1)
	State *int `json:"state"` // kernel cooling state 0-4 (cooling device `pwm-fan`)
}

// cpuJiffy is the per-tick /proc/stat aggregate (idle, total jiffies).
type cpuJiffy struct {
	idle  uint64
	total uint64
}

// cpuUtilPct is the 2-sample utilization delta, in percent.
func cpuUtilPct(prev, now *cpuJiffy) float64 {
	if prev == nil || now == nil || now.total <= prev.total {
		return 0
	}
	idleDelta := now.idle - prev.idle
	totalDelta := now.total - prev.total
	return float64(totalDelta-idleDelta) / float64(totalDelta) * 100
}

type sampler struct {
	mu        sync.RWMutex
	ring      [ringSize]sample
	pos       int      // next slot to write
	count     int      // usable samples (<= ringSize)
	zoneTypes []string // discovered thermal zone types, sorted
	zoneIdx   map[string]int
	prevCPU   *cpuJiffy
}

func newSampler() *sampler {
	s := &sampler{zoneIdx: make(map[string]int)}
	// Thermal zones are numbered densely thermal_zone0..N; discover by type
	// (no hardcoded zone list — an added/renamed zone shows up automatically,
	// plan §6).
	for i := 0; ; i++ {
		t, err := readTrim("/sys/class/thermal/thermal_zone" + strconv.Itoa(i) + "/type")
		if err != nil || t == "" {
			break
		}
		if _, dup := s.zoneIdx[t]; !dup {
			s.zoneTypes = append(s.zoneTypes, t)
		}
		s.zoneIdx[t] = i
	}
	sort.Strings(s.zoneTypes)
	return s
}

// run ticks every tickInterval until ctx is done, then closes done.
func (s *sampler) run(ctx context.Context, done chan<- struct{}) {
	defer close(done)
	for {
		s.tick()
		select {
		case <-ctx.Done():
			return
		case <-time.After(tickInterval):
		}
	}
}

func (s *sampler) tick() {
	s.mu.Lock()
	defer s.mu.Unlock()

	cpuNow := readCPUJiffy()
	var cpu *float64
	if s.prevCPU != nil && cpuNow != nil {
		pct := cpuUtilPct(s.prevCPU, cpuNow)
		cpu = &pct
	}
	if cpuNow != nil {
		s.prevCPU = cpuNow
	}

	smp := sample{
		ts:      time.Now(),
		temps:   s.readTempsLocked(),
		mem:     readMeminfo(),
		disk:    readDisk("/"),
		cpuUtil: cpu,
		load:    readLoad(),
		fan:     readFan(),
	}
	s.ring[s.pos] = smp
	s.pos = (s.pos + 1) % ringSize
	if s.count < ringSize {
		s.count++
	}
}

// snapshot returns a copy of the newest sample (zero ts when no tick has
// landed yet) plus the discovered zone set.
func (s *sampler) snapshot() (sample, []string) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.count == 0 {
		return sample{}, s.zoneTypes
	}
	return cloneSample(s.ring[(s.pos+ringSize-1)%ringSize]), s.zoneTypes
}

// series returns newest-first samples fresh enough for the last N minutes
// (the window is generous by one tick so the current-minute bucket is always
// covered; the history handler does the exact bucketing).
func (s *sampler) series(minutes int) []sample {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.count == 0 {
		return nil
	}
	window := time.Duration(minutes)*time.Minute + tickInterval
	now := time.Now()
	var out []sample
	for i := 0; i < s.count; i++ {
		smp := s.ring[(s.pos+ringSize-1-i)%ringSize] // newest first
		if now.Sub(smp.ts) > window {
			break // ring iterates newest->oldest; the rest are older
		}
		out = append(out, cloneSample(smp))
	}
	return out
}

func cloneSample(smp sample) sample {
	cp := smp
	if smp.temps != nil {
		cp.temps = make(map[string]*float64, len(smp.temps))
		for k, v := range smp.temps {
			if v != nil {
				v2 := *v
				cp.temps[k] = &v2
			}
		}
	}
	if smp.disk != nil {
		d := *smp.disk
		cp.disk = &d
	}
	if smp.cpuUtil != nil {
		v := *smp.cpuUtil
		cp.cpuUtil = &v
	}
	if smp.fan != nil {
		f := *smp.fan
		cp.fan = &f
	}
	for i := range smp.load {
		if smp.load[i] != nil {
			v := *smp.load[i]
			cp.load[i] = &v
		}
	}
	return cp
}
