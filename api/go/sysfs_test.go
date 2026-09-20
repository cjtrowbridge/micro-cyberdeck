package main

import "testing"

// readCPUJiffy parses the live /proc/stat aggregate line (total must be
// strictly positive; idle must be one of those jiffies).
func TestReadCPUJiffy(t *testing.T) {
	j := readCPUJiffy()
	if j == nil || j.total == 0 || j.idle > j.total {
		t.Fatalf("readCPUJiffy() = %+v, want non-nil with idle<=total>0", j)
	}
}
