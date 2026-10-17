"""Read-only integrity and authored-guide link checks; no network/dependencies."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


def main() -> int:
    packet = Path(__file__).resolve().parent
    repo = packet.parents[2]
    failures: list[str] = []
    manifest = json.loads((packet / 'source-manifest.json').read_text(encoding='utf-8'))
    seen: set[str] = set()
    for entry in manifest['files']:
        name = entry['path']
        target = (packet / name).resolve()
        if name in seen:
            failures.append(f'duplicate manifest path: {name}')
        seen.add(name)
        if not target.is_relative_to(packet):
            failures.append(f'path outside packet: {name}')
            continue
        if not target.is_file():
            failures.append(f'missing snapshot: {name}')
            continue
        data = target.read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            failures.append(f'snapshot bytes/hash mismatch: {name}')
    for target in (packet / 'sources').rglob('*'):
        if target.is_file() and target.relative_to(packet).as_posix() not in seen:
            failures.append(f'unmanifested snapshot: {target.relative_to(packet)}')
    guides = ['README.md', 'acquisition.md', 'npu.md', 'stt.md', 'insertion.md', 'validation.md']
    link_count = 0
    for name in guides:
        guide = packet / name
        for value in re.findall(r'\]\(([^)]+)\)', guide.read_text(encoding='utf-8')):
            parsed = urlsplit(value)
            if parsed.scheme or value.startswith('#'):
                continue
            target = (guide.parent / unquote(parsed.path)).resolve()
            link_count += 1
            if not target.is_relative_to(repo) or not target.exists():
                failures.append(f'{name}: unresolved/outside-repo link {value}')
    inventory = json.loads((packet / 'artifact-inventory.json').read_text(encoding='utf-8'))
    for key in ['sherpa-runtime', 'sherpa-models', 'vosk-python', 'sherpa-onnx-python', 'sherpa-onnx-core-python']:
        if key not in inventory or 'error' in inventory[key]:
            failures.append(f'missing acquisition metadata: {key}')
    for error in failures:
        print(f'FAIL {error}')
    print(f'Packet: snapshots={len(seen)} guide_links={link_count} failures={len(failures)}')
    print('Scope: file integrity and local guide links only; no downloads or runtime/device validation.')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
