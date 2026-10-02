#!/usr/bin/env python3
"""Rebuild StoryFlip with the pinned Momentum SDK (requires Python + ufbt)."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent
URL = 'https://up.momentum-fw.dev/builds/firmware/dev/flipper-z-f7-sdk-mntm-dev-d3f89dfe.zip'
SHA256 = '238db260ee7e8f6e9a79dd1c760e07a832351a7d92d896b4cb19082f8548b2c1'

def main():
    cache = ROOT / '.build-sdk'
    cache.mkdir(exist_ok=True)
    archive = cache / 'momentum-d3f89dfe.zip'
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as response, archive.open('wb') as out:
            while chunk := response.read(1024 * 1024):
                out.write(chunk)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit('SDK checksum mismatch. Remove .build-sdk/momentum-d3f89dfe.zip and retry.')
    env = os.environ.copy()
    env['UFBT_HOME'] = str(cache / 'ufbt')
    subprocess.run([sys.executable, '-m', 'ufbt', 'update', '--hw-target', 'f7', '--local', str(archive)], env=env, cwd=ROOT, check=True)
    subprocess.run([sys.executable, '-m', 'ufbt'], env=env, cwd=ROOT, check=True)
    print(ROOT / 'dist' / 'storyflip.fap')

if __name__ == '__main__':
    main()
