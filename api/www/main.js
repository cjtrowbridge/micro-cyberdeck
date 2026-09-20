/* cyberdeck — System tab.
 * Polls /api/metrics + /api/history?minutes=60 every 5 s (single-flight:
 * a tick is skipped if the previous one is still in flight). A failed
 * request only turns the freshness dot — the last good data stays on
 * screen (the board may be offline or the API briefly down).
 */
"use strict";

const POLL_MS = 5000;

const $ = (id) => document.getElementById(id);

let tempChart = null; // Chart.js instance
let lastGood = 0; // ms: last successful poll (freshness dot)

// Stable, legible per-zone colors for the SoC zone set (fallback: hue by index).
const ZONE_COLORS = {
  cpub:      "#ff7b72",
  cpul:      "#79c0ff",
  cpub_idle: "#ffa657",
  cpul_idle: "#56d364",
  ddr:       "#d2a8ff",
  gpu:       "#e3b341",
  npu:       "#39c5cf",
  skin:      "#8b949e",
};

function zoneName(t) { return (t || "").replace(/_thermal_zone$/, ""); }

function seriesColor(i, label) {
  if (label && ZONE_COLORS[label]) return ZONE_COLORS[label];
  return "hsl(" + ((i * 47) % 360) + " 70% 60%)";
}

function fmtBytes(n) {
  if (n == null || n < 0) return "\u2014";
  const u = ["B", "KiB", "MiB", "GiB", "TiB"];
  let i = 0;
  let v = Number(n);
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return (i === 0 ? Math.round(v) : v.toFixed(1)) + " " + u[i];
}

// °C -> green->red color ramp for the zone chips.
function tempColor(c) {
  if (c == null) return "#7d8aa0";
  const t = Math.max(0, Math.min(1, (c - 35) / 35));
  const a = [56, 209, 127]; // ok green
  const b = [255, 95, 86];  // hot red
  const m = a.map((x, i) => Math.round(x + (b[i] - x) * t));
  return "rgb(" + m.join(",") + ")";
}

function fmtClock(t) {
  const d = new Date(t);
  if (isNaN(d.getTime())) return "";
  return d.toTimeString().slice(0, 5); // local HH:MM
}

// ── renderers (each is pure: take the last good data, paint it) ──────────

function renderHost() {
  $("deck-host").textContent = window.location.hostname || "cyberdeck";
}

function renderUptime(s) {
  if (s == null) { $("uptime-chip").textContent = "up \u2014"; return; }
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  $("uptime-chip").textContent = d > 0 ? `up ${d}d ${h}h ${m}m` : `up ${h}h ${m}m`;
}

function renderTemps(m) {
  const box = $("zone-chips");
  box.innerHTML = "";
  for (const [z, c] of Object.entries(m.temps || {})) {
    const el = document.createElement("div");
    el.className = "zone";
    const name = document.createElement("span");
    name.className = "zname";
    name.textContent = zoneName(z);
    const val = document.createElement("span");
    val.className = "zval";
    val.textContent = c == null ? "\u2014" : c.toFixed(1) + "\u00b0C";
    val.style.color = tempColor(c);
    el.appendChild(name);
    el.appendChild(val);
    box.appendChild(el);
  }
}

function renderMem(m) {
  const mm = m.mem || {};
  const total = mm.total_bytes || 0;
  const used = mm.used_bytes || 0;
  const pct = total ? (used / total) * 100 : 0;
  $("mem-bar").style.width = pct.toFixed(1) + "%";
  $("mem-label").textContent = pct.toFixed(1) + " % used";
  $("mem-kv").textContent =
    "used       " + fmtBytes(used) + "\n" +
    "available  " + fmtBytes(mm.available_bytes) + "\n" +
    "total      " + fmtBytes(total);
  const st = mm.swap_total_bytes || 0;
  const sf = mm.swap_free_bytes || 0;
  $("mem-swap").textContent =
    st > 0
      ? "swap       " + fmtBytes(st - sf) + " / " + fmtBytes(st)
      : "swap       \u2014 (none)";
}

function renderDisk(d) {
  if (!d) {
    $("disk-label").textContent = "\u2014";
    $("disk-kv").textContent = "statfs failed";
    return;
  }
  const pct = d.pct || 0;
  $("disk-bar").style.width = Math.min(100, pct) + "%";
  $("disk-bar").classList.toggle("hot", pct > 80);
  $("disk-label").textContent = pct.toFixed(1) + " % used" + (pct > 80 ? " \u25b2" : "");
  $("disk-kv").textContent =
    "used   " + fmtBytes(d.used_bytes) + "\n" +
    "free   " + fmtBytes(d.free_bytes) + "\n" +
    "total  " + fmtBytes(d.total_bytes);
}

function renderFan(f) {
  if (!f) { $("fan-kv").textContent = "fan  \u2014"; return; }
  const duty = f.pwm == null ? null : (f.pwm / 255) * 100;
  $("fan-kv").textContent =
    "duty    " + (duty == null ? "\u2014" : duty.toFixed(0) + " % (" + f.pwm + "/255)") + "\n" +
    "state   " + (f.state == null ? "\u2014" : f.state + "/4 (kernel governor)");
}

// ── chart (Chart.js, data mutation — no full re-render on the 5 s tick) ──

function buildChart(labels, rows) {
  const ctx = $("temp-chart").getContext("2d");
  tempChart = new Chart(ctx, {
    type: "line",
    data: {
      labels,
      datasets: rows.map((r, i) => ({
        label: zoneName(r.zone),
        data: r.values,
        borderColor: seriesColor(i, zoneName(r.zone)),
        backgroundColor: "transparent",
        borderWidth: 1.5,
        pointRadius: 0,
        tension: 0.25,
      })),
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: {
          ticks: { color: "#7d8aa0", maxTicksLimit: 8, maxRotation: 0 },
          grid: { color: "#1e2432" },
        },
        y: {
          title: { display: true, text: "\u00b0C", color: "#7d8aa0" },
          ticks: { color: "#7d8aa0" },
          grid: { color: "#1e2432" },
          suggestedMin: 25,
        },
      },
      plugins: {
        legend: {
          labels: { color: "#dbe2ee", usePointStyle: true, pointStyle: "line" },
        },
      },
    },
  });
}

function renderChart(history) {
  // zone identity = the API's discovered set (stable order from the sampler)
  const order = [];
  // derive from history temps keys, first point that has them (all points share the set)
  for (const p of history.series) {
    if (p.temps && Object.keys(p.temps).length) {
      // keep a stable order across points: first non-empty point's order
      for (const k in p.temps) order.push(k);
      break;
    }
  }
  const labels = history.series.map((p) => fmtClock(p.t));
  const rows = order.map((z) => ({
    zone: z,
    values: history.series.map((p) => (p.temps ? p.temps[z] : null)),
  }));
  if (!tempChart) {
    buildChart(labels, rows);
    return;
  }
  if (labels.length === tempChart.data.labels.length) {
    // in-place mutation: the live point moves, the chart is NOT rebuilt
    for (let i = 0; i < rows.length; i++) {
      if (tempChart.data.datasets[i]) tempChart.data.datasets[i].data = rows[i].values;
    }
    tempChart.update();
  } else {
    buildChart(labels, rows); // only when the point count itself changes
  }
}

// ── freshness dot (green < 10 s, amber < 30 s, red otherwise) ────────────

function setDot(now) {
  const age = lastGood ? (now - lastGood) / 1000 : Infinity;
  const el = $("fresh-dot");
  let cls, txt;
  if (age < 10) { cls = "fresh"; txt = "live"; }
  else if (age < 30) { cls = "stale"; txt = "degraded"; }
  else { cls = "dead"; txt = "stale"; }
  el.className = "chip " + cls;
  el.innerHTML = "&bull; " + txt;
}

// ── 5 s poller, single-flight ────────────────────────────────────────────

let inFlight = false;

async function poll() {
  if (inFlight) return; // single-flight: skip this tick
  inFlight = true;
  try {
    const [m, h] = await Promise.all([
      fetch("/api/metrics", { cache: "no-store" }).then((r) => {
        if (!r.ok) throw new Error("metrics HTTP " + r.status);
        return r.json();
      }),
      fetch("/api/history?minutes=60", { cache: "no-store" }).then((r) => {
        if (!r.ok) throw new Error("history HTTP " + r.status);
        return r.json();
      }),
    ]);
    renderUptime(m.uptime_s);
    renderTemps(m);
    renderMem(m);
    renderDisk(m.disk);
    renderFan(m.fan);
    if (h.series && h.series.length) renderChart(h);
    lastGood = Date.now();
    setDot(lastGood);
  } catch (err) {
    // keep last good data; the dot tells the story
    setDot(Date.now());
  } finally {
    inFlight = false;
  }
}

window.addEventListener("DOMContentLoaded", () => {
  renderHost();
  poll();
  setInterval(poll, POLL_MS);
  setInterval(() => setDot(Date.now()), 1000);
});