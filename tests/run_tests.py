#!/usr/bin/env python3
"""Run production C core tests on Linux with an optional NFC test collection."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--collection', type=Path, help='optional ZIP archive containing SLIX NFC test files')
args = parser.parse_args()
base = Path(__file__).resolve().parents[1]
app = base / 'storyflip' if (base / 'storyflip' / 'storyflip.c').exists() else base
tests = base / 'tests'
env = os.environ.copy()
env['ASAN_OPTIONS'] = 'detect_leaks=0'  # LeakSanitizer is unavailable in some managed runtimes.
with tempfile.TemporaryDirectory(prefix='storyflip-test-') as temporary:
    temp = Path(temporary)
    common = ['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-I'+str(tests/'include'), '-I'+str(app), str(app/'sf_store.c'), str(app/'sf_browser.c'), str(app/'sf_i18n.c')]
    executable = temp/'store_test'
    subprocess.run(common+[str(tests/'store_test.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(temp)], env=env, check=True)
    browser_dir = temp/'browser'
    browser_dir.mkdir()
    executable = temp/'browser_test'
    subprocess.run(common+[str(tests/'browser_test.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(browser_dir)], env=env, check=True)
    if args.collection:
        corpus = temp/'corpus'
        with zipfile.ZipFile(args.collection.resolve()) as archive:
            for entry in archive.infolist():
                if not entry.filename.lower().endswith('.nfc'): continue
                target = (corpus/entry.filename).resolve()
                if not target.is_relative_to(corpus): raise ValueError('Unsafe archive path')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(entry))
        collection_root = corpus
        children = list(corpus.iterdir())
        if len(children) == 1 and children[0].is_dir():
            collection_root = children[0]
        subprocess.run([sys.executable, str(tests/'generate_core_test.py')], cwd=base, check=True)
        executable = temp/'core_test'
        subprocess.run(common+[str(tests/'core_test.generated.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable), str(temp), str(collection_root)], env=env, check=True)
