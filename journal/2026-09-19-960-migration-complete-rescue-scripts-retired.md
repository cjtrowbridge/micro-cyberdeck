# 2026-09-19 — 960 single-screen migration held; rescue scripts retired

## What changed

The one-time 480→960 desktop migration (2026-09-13; `apply-960.sh` /
`revert-480.sh` at the repo root) has held — the user confirmed the 960
single-screen setup has not failed. Both migration scripts are retired
(`git rm`), and the docs that named them (`README.md`, `AGENTS.md`,
`docs/setup.md`, `docs/hardware/display-st7789.md`) now state the
bootstrap as the single 960x960 `:1` desktop.

## Why no setup.sh behavior change

`setup.sh` **already** encodes the 960-only desired state — it never
referenced either script:

- `DESIRED_WIDTH=960` / `DESIRED_HEIGHT=960` / `DESIRED_DEPTH=24` (§1)
- the five `gamepi-*-service` templates are a single-`:1` stack (no `:2`
  units, no 480 anywhere in the file)
- the bridge template is `SOURCE_W/H = 960` (4:1 down-scale)
- verify items `xvfb-screen` (greps `dimensions: 960x960`) and
  `bridge-live` (greps `X11 root: 960x960`) only pass at 960

So "the bootstrap assumes 960" was already true; only the two one-time
helper scripts and their four doc citations were out of date. No
provisioning-behavior change → no managed-scope / verify-matrix diff in
`docs/setup.md`, just the Troubleshooting note no longer pointing at
removed files. Acceptance evidence: a live `setup.sh --plan` audit run
with this change in place (2026-09-19) reported every managed artifact
`unchanged`, all 15 verify rows PASS, `RESULT: CONVERGED` — no writes,
no reboot, no behavior diff.

## Why the scripts were safe to drop

- `apply-960.sh` ran exactly once (2026-09-13). Its own guard refuses a
  re-run while any `*.before-960` backup exists, so it could never have
  been re-applied anyway; it had no re-entrant role.
- `revert-480.sh` exists solely to undo that one change by restoring the
  three `*.before-960` backups to the **480** two-desktop configuration
  (`:1` 480x480 + `:2` 1280x720). The 480 mode was the broken state the
  migration left behind, and 960 held — the escape hatch would have
  resurrected the broken configuration.
- Probed on the live board 2026-09-19: `gamepi-xvfb` / `gamepi-lcd`
  **active** on 960; the legacy `vnc-xvfb` / `vnc-openbox` /
  `vnc-highres` (`:2`) units are `disabled` / `inactive`, no `Xvfb :2`
  process, nothing listening on `:5901`; the three `*.before-960`
  backups (2026-09-13) remain on disk as the only artifacts the scripts
  ever touched.

## Deliberately NOT changed

- **Orphaned `:2` unit files stay on the board** (`/etc/systemd/system/vnc-{xvfb,openbox,highres}.service`, all
  disabled): removing them is a one-time manual cleanup with zero
  function; `setup.sh` neither manages nor reports on them, and the
  verify matrix asserts the single `:1` path only. If a future cleanup
  removes them, note it in the journal — no script change needed.
- **`*.before-960` backup files remain on disk** (the 480 config's only
  snapshot, dated 2026-09-13): they cost nothing and the display record
  cites their existence as the provenance trail.

## References

- `docs/setup.md` — managed scope (five `gamepi-*` units, bridge,
  autostart: all 960-canonical)
- `docs/hardware/display-st7789.md` — resolution-history bullet rebased on
  this retirement
- `README.md` Getting Started bullet, `AGENTS.md` host-ownership line