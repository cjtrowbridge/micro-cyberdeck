package main

import (
	"encoding/json"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

// handleHealth — the verify/probe target: minimal JSON, loopback + Apache.
func handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write([]byte(`{"ok":true}`))
}

type memOut struct {
	TotalBytes     uint64 `json:"total_bytes"`
	UsedBytes      uint64 `json:"used_bytes"`
	AvailableBytes uint64 `json:"available_bytes"`
	SwapTotalBytes uint64 `json:"swap_total_bytes"`
	SwapFreeBytes  uint64 `json:"swap_free_bytes"`
}

type loadOut struct {
	Load1  *float64 `json:"load1"`
	Load5  *float64 `json:"load5"`
	Load15 *float64 `json:"load15"`
}

type metricsOut struct {
	TS       string              `json:"ts"`
	Temps    map[string]*float64 `json:"temps"`
	Mem      memOut              `json:"mem"`
	Disk     *diskSample         `json:"disk"`
	CPUUtil  *float64            `json:"cpu_util_pct"`
	Load     loadOut             `json:"load"`
	UptimeS  *float64            `json:"uptime_s"`
	Fan      *fanState           `json:"fan"`
	Hostname string              `json:"hostname"`
}

// handleMetrics — current snapshot (plan §7). Everything else on :8080 is a
// 404 (mux default) — no write endpoints.
func (s *sampler) handleMetrics(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	smp, zones := s.snapshot()

	out := metricsOut{
		TS:      smp.ts.UTC().Format(time.RFC3339),
		Temps:   smp.temps,
		Disk:    smp.disk,
		CPUUtil: smp.cpuUtil,
		Fan:     smp.fan,
	}
	if out.Temps == nil {
		out.Temps = make(map[string]*float64, len(zones))
	}
	for _, z := range zones {
		if _, ok := out.Temps[z]; !ok {
			out.Temps[z] = nil // discovered but unreadable this tick
		}
	}
	host, err := os.Hostname()
	if err == nil {
		out.Hostname = host
	}
	if smp.ts.IsZero() {
		out.TS = time.Now().UTC().Format(time.RFC3339) // first tick in flight (brief, at start)
		writeJSON(w, out)
		return
	}

	out.Mem = memOut{
		TotalBytes:     smp.mem.total * 1024,
		UsedBytes:      smp.mem.used * 1024,
		AvailableBytes: smp.mem.available * 1024,
		SwapTotalBytes: smp.mem.swapTotal * 1024,
		SwapFreeBytes:  smp.mem.swapFree * 1024,
	}
	for i, v := range smp.load {
		switch i {
		case 0:
			out.Load.Load1 = v
		case 1:
			out.Load.Load5 = v
		case 2:
			out.Load.Load15 = v
		}
	}
	if line, err := readTrim("/proc/uptime"); err == nil {
		if f, err := strconv.ParseFloat(strings.Fields(line)[0], 64); err == nil {
			out.UptimeS = &f
		}
	}
	writeJSON(w, out)
}

type histPoint struct {
	T             string              `json:"t"`
	Temps         map[string]*float64 `json:"temps"`
	MemUsedBytes  *uint64             `json:"mem_used_bytes"`
	DiskUsedBytes *uint64             `json:"disk_used_bytes"`
}

type historyOut struct {
	StepS  int         `json:"step_s"`
	From   string      `json:"from,omitempty"`
	To     string      `json:"to,omitempty"`
	Series []histPoint `json:"series"`
}

// handleHistory — per-minute buckets (avg of the enclosing 5 s samples) over
// the last N minutes; the current-minute bucket is a LIVE partial — the
// chart's last point therefore moves every 5 s tick (plan §4).
func (s *sampler) handleHistory(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	minutes := 60
	if m := r.URL.Query().Get("minutes"); m != "" {
		if v, err := strconv.Atoi(m); err == nil && v > 0 {
			minutes = v
		}
	}

	type agg struct {
		memSum, diskSum float64
		memN, diskN     int
		tSum            map[string]float64
		tN              map[string]int
	}
	nowMinute := time.Now().Unix() / 60 * 60
	// The last `minutes` completed minutes PLUS the live in-progress minute
	// as the final point (N+1 points; that last point moves every 5 s tick
	// — plan §4/§7).
	buckets := make(map[int64]*agg, minutes+1)
	for k := 0; k <= minutes; k++ {
		buckets[nowMinute-int64(k)*60] = &agg{
			tSum: make(map[string]float64),
			tN:   make(map[string]int),
		}
	}
	for _, smp := range s.series(minutes + 1) {
		b, ok := buckets[smp.ts.Unix()/60*60]
		if !ok {
			continue
		}
		b.memSum += float64(smp.mem.used)
		b.memN++
		if smp.disk != nil {
			b.diskSum += float64(smp.disk.Used)
			b.diskN++
		}
		for z, c := range smp.temps {
			if c != nil {
				b.tSum[z] += *c
				b.tN[z]++
			}
		}
	}

	out := historyOut{StepS: 60, Series: make([]histPoint, 0, minutes+1)}
	var fromTS, toTS time.Time
	for k := minutes; k >= 0; k-- {
		bk := nowMinute - int64(k)*60
		b := buckets[bk]
		ts := time.Unix(bk, 0).UTC()
		if fromTS.IsZero() {
			fromTS = ts
		}
		toTS = ts

		p := histPoint{T: ts.Format(time.RFC3339), Temps: make(map[string]*float64)}
		for z, sum := range b.tSum {
			n := b.tN[z]
			if n > 0 {
				v := float64(int64(sum/float64(n)*10+0.5)) / 10.0 // 0.1 °C
				p.Temps[z] = &v
			}
		}
		if b.memN > 0 {
			v := uint64(b.memSum/float64(b.memN)+0.5) * 1024 // kB -> bytes
			p.MemUsedBytes = &v
		}
		if b.diskN > 0 {
			v := uint64(b.diskSum/float64(b.diskN) + 0.5)
			p.DiskUsedBytes = &v
		}
		out.Series = append(out.Series, p)
	}
	if !fromTS.IsZero() {
		out.From = fromTS.Format(time.RFC3339)
		out.To = toTS.Format(time.RFC3339)
	}
	writeJSON(w, out)
}

func writeJSON(w http.ResponseWriter, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}
