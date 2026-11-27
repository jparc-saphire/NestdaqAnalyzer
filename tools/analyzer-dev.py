#!/usr/bin/env python3
"""Run your own build of the analyzer next to a running DAQ, and compare results of two builds.

nda-replay only reads the run file, so any number of people can analyze the file of a running DAQ at the same time,
each with a build of their own branch, without touching the DAQ or the analyzer that the shift members watch.

  run      analyze one run file (--run N or --latest), or keep following the newest run file of the data directory
           (--follow-new). Each run gives runNNNNNN.root, runNNNNNN.json (summary) and analyzer-runNNNNNN.log in --out.
           No browser is needed; --http ADDR:PORT (loopback only) also serves the histograms on a port of your own.
  compare  compare two results of the same run (two .root files or two summary .json files), for example the
           analyzer that is in use and your build on a reference run. Exit status 0: same, 1: different.

The analyzer runs with a lower CPU priority (--nice, default 10). Nothing is written into the data directory.
"""
import argparse
import hashlib
import json
import math
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
RUN_FILE = re.compile(r'^run(\d{6})\.dat$')
LOOPBACK = ('127.0.0.1', 'localhost', '::1', '[::1]')
# Exit status of nda-replay after the Restart button of its page (--allow-restart).
RESTART_STATUS = 75


def run_filename(number):
    if not isinstance(number, int) or isinstance(number, bool) or not 0 <= number <= 999999:
        raise SystemExit('analyzer-dev: run number must be an integer from 0 through 999999')
    return f'run{number:06d}.dat'


def run_files(data_dir):
    """{run number: path} of the run files in data_dir."""
    out = {}
    for p in Path(data_dir).iterdir():
        m = RUN_FILE.match(p.name)
        if m and p.is_file():
            out[int(m.group(1))] = p
    return out


def newest_run(data_dir):
    """Run number of the most recently modified non-empty run file, or None."""
    best = None
    for run, p in run_files(data_dir).items():
        try:
            st = p.stat()
        except OSError:
            continue
        if st.st_size == 0:
            continue
        key = (st.st_mtime, run)
        if best is None or key > best[0]:
            best = (key, run)
    return None if best is None else best[1]


def inside(path, directory):
    path, directory = Path(path).resolve(), Path(directory).resolve()
    return path == directory or directory in path.parents


def port_free(address):
    """True when ADDR:PORT can be bound now."""
    import socket
    host, port = address.rsplit(':', 1)
    with socket.socket() as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind((host, int(port)))
        except OSError:
            return False
    return True


def parse_http(address):
    """'127.0.0.1:18301' -> same string; rejects anything that is not ADDR:PORT."""
    host, sep, port = address.rpartition(':')
    if not sep or not host or not port.isdigit() or not 0 < int(port) < 65536:
        raise ValueError(f'address must be ADDR:PORT: {address!r}')
    return address


def build_info(exe):
    """Where the analyzer executable comes from: its sha256 and, when it is in a CMake build directory of a Git
    clone, the source directory, commit, branch and whether the clone has uncommitted changes."""
    exe = Path(exe).resolve()
    info = {'executable': str(exe), 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
    for d in exe.parents:
        cache = d / 'CMakeCache.txt'
        if not cache.is_file():
            continue
        info['build_dir'] = str(d)
        for line in cache.read_text(errors='replace').splitlines():
            if line.startswith('CMAKE_HOME_DIRECTORY:'):
                src = line.split('=', 1)[1].strip()
                info['source_dir'] = src
                git = ['git', '-C', src]
                try:
                    info['commit'] = subprocess.run(git + ['rev-parse', 'HEAD'], capture_output=True, text=True,
                                                    check=True).stdout.strip()
                    info['branch'] = subprocess.run(git + ['rev-parse', '--abbrev-ref', 'HEAD'], capture_output=True,
                                                    text=True, check=True).stdout.strip()
                    dirty = subprocess.run(git + ['status', '--porcelain', '--untracked-files=no'],
                                           capture_output=True, text=True, check=True).stdout.strip()
                    info['uncommitted_changes'] = bool(dirty)
                except (OSError, subprocess.CalledProcessError):
                    info['commit'] = None
                break
        break
    return info


# ---------------------------------------------------------------------------------------------------------- run

def replay_command(a, data_file, out_dir, run):
    cmd = [str(Path(a.exe).resolve()), '--input', str(data_file), '--follow', '--quiet',
           '--output', str(out_dir / f'run{run:06d}.root'), '--summary-json', str(out_dir / f'run{run:06d}.json')]
    if a.idle_timeout > 0:
        cmd += ['--idle-timeout', f'{a.idle_timeout:g}']
    if a.http:
        cmd += ['--http', f'http:{a.http}', '--keep-alive', '--allow-restart']
    if a.param:
        cmd += ['--param', str(Path(a.param).resolve())]
    return cmd + list(a.extra)


def spawn(command, log_path, nice):
    with open(log_path, 'ab') as log:
        return subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                preexec_fn=(lambda: os.nice(nice)) if nice else None)


def stop(proc, timeout=30):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def check_run_args(a):
    """Refuse settings that could disturb the DAQ or the analyzer in use."""
    data_dir = Path(a.data_dir)
    if not data_dir.is_dir():
        raise SystemExit(f'analyzer-dev: no data directory {data_dir}')
    out = Path(a.out)
    if out.exists() or out.is_symlink():
        raise SystemExit(f'analyzer-dev: --out must be a new directory: {out}')
    if inside(out, data_dir):
        raise SystemExit('analyzer-dev: --out must not be inside the data directory')
    if not os.access(a.exe, os.X_OK):
        raise SystemExit(f'analyzer-dev: not an executable: {a.exe} (build nda-replay first, or give --exe)')
    if a.http:
        try:
            parse_http(a.http)
        except ValueError as exc:
            raise SystemExit(f'analyzer-dev: {exc}')
        if a.http.rsplit(':', 1)[0] not in LOOPBACK:
            raise SystemExit('analyzer-dev: --http must be a loopback address (127.0.0.1:PORT); '
                             'view it through an SSH tunnel')
        if not port_free(a.http):
            raise SystemExit(f'analyzer-dev: {a.http} is in use (the analyzer in use or another developer); '
                             'take another port')
    if a.param and not Path(a.param).is_file():
        raise SystemExit(f'analyzer-dev: no parameter index {a.param}')


def cmd_run(a):
    check_run_args(a)
    data_dir = Path(a.data_dir).resolve()
    if a.latest:
        a.run = newest_run(data_dir)
        if a.run is None:
            raise SystemExit(f'analyzer-dev: no run file in {data_dir}')
    if not a.follow_new:
        data_file = data_dir / run_filename(a.run)
        if not data_file.is_file():
            raise SystemExit(f'analyzer-dev: no run file {data_file}')
    out = Path(a.out)
    out.mkdir(parents=True)
    out = out.resolve()
    report = {'format': 'analyzer-dev-v1', 'start': time.time(), 'data_dir': str(data_dir),
              'mode': 'follow-new' if a.follow_new else 'single', 'http': a.http, 'nice': a.nice,
              'param': str(Path(a.param).resolve()) if a.param else None, 'analyzer': build_info(a.exe), 'runs': []}

    def save():
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    save()
    stopped = {'flag': False}

    def on_signal(signum, frame):
        stopped['flag'] = True
    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    where = report['analyzer'].get('commit') or report['analyzer']['executable']
    print(f'analyzer-dev: analyzer {where}'
          f'{" (uncommitted changes)" if report["analyzer"].get("uncommitted_changes") else ""}', flush=True)
    link = f', http://{a.http}/' if a.http else ''

    def start(run, record=None):
        command = replay_command(a, data_dir / run_filename(run), out, run)
        if record is None:  # a restart from the page keeps the record of its run
            record = {'run_number': run, 'start': time.time()}
            report['runs'].append(record)
        record['command'] = command
        return spawn(command, out / f'analyzer-run{run:06d}.log', a.nice), record

    if not a.follow_new:
        print(f'analyzer-dev: run {a.run} -> {out}{link}', flush=True)
        proc, record = start(a.run)
        save()
        while True:
            if proc.poll() is not None:
                if proc.returncode == RESTART_STATUS:  # the Restart button of the page
                    record.setdefault('restarts', []).append(time.time())
                    proc, _ = start(a.run, record)
                    continue
                break
            if stopped['flag']:
                stop(proc)
                break
            time.sleep(0.2)
        record.update(end=time.time(), returncode=proc.returncode)
        report['end'] = time.time()
        save()
        print(f'analyzer-dev: done (exit {proc.returncode}); {out}', flush=True)
        return 0 if proc.returncode in (0, -signal.SIGTERM) else 1

    # --follow-new: the newest run file now and every newer one, like the analyzer of a running DAQ.
    print(f'analyzer-dev: following the newest run file of {data_dir} -> {out}{link}; stop with Ctrl-C', flush=True)
    current = None  # {'run', 'proc', 'record'}
    done = set()    # runs whose analyzer has ended: not started again
    try:
        while not stopped['flag']:
            if current and current['proc'].poll() is not None:
                rc = current['proc'].returncode
                if rc == RESTART_STATUS:
                    current['record'].setdefault('restarts', []).append(time.time())
                    current['proc'], _ = start(current['run'], current['record'])
                else:
                    current['record'].update(end=time.time(), returncode=rc, stopped='exited')
                    done.add(current['run'])
                    current = None
            run = newest_run(data_dir)
            if run is not None and run not in done and (current is None or current['run'] != run):
                if current:
                    stop(current['proc'])
                    current['record'].update(end=time.time(), returncode=current['proc'].returncode,
                                             stopped='next run')
                    done.add(current['run'])
                print(f'analyzer-dev: run {run}', flush=True)
                proc, record = start(run)
                current = {'run': run, 'proc': proc, 'record': record}
            save()
            time.sleep(a.poll)
    finally:
        if current:
            stop(current['proc'])
            current['record'].update(end=time.time(), returncode=current['proc'].returncode, stopped='session end')
        report['end'] = time.time()
        save()
    print(f'analyzer-dev: stopped; {out}', flush=True)
    return 0


# ------------------------------------------------------------------------------------------------------ compare

def flatten(obj, prefix=''):
    """Numbers and texts of a summary JSON as {'path': value}; lists of objects with an 'id' are keyed by it."""
    out = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            out.update(flatten(v, f'{prefix}{k}.'))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            key = v.get('id', i) if isinstance(v, dict) else i
            out.update(flatten(v, f'{prefix}{key}.'))
    else:
        out[prefix[:-1]] = obj
    return out


# Fields that differ between two analyses of the same data without meaning anything.
JSON_IGNORE = re.compile(r'(^|\.)(path|run_seconds|wall_seconds)$')


def close(x, y, tolerance):
    if x == y:
        return True
    if isinstance(x, float) and isinstance(y, float) and math.isnan(x) and math.isnan(y):
        return True
    return abs(x - y) <= tolerance * max(abs(x), abs(y))


def compare_json(fa, fb, tolerance):
    a, b = flatten(json.loads(Path(fa).read_text())), flatten(json.loads(Path(fb).read_text()))
    diffs = []
    for k in sorted(set(a) | set(b)):
        if JSON_IGNORE.search(k):
            continue
        if k not in a or k not in b:
            diffs.append(f'{k}: only in {"A" if k in a else "B"}')
            continue
        va, vb = a[k], b[k]
        if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and not isinstance(va, bool):
            if not close(va, vb, tolerance):
                diffs.append(f'{k}: {va} -> {vb}')
        elif va != vb:
            diffs.append(f'{k}: {va!r} -> {vb!r}')
    return diffs


def root_histograms(path):
    import ROOT  # noqa: PLC0415 (only the ROOT comparison needs it)
    ROOT.gROOT.SetBatch(True)
    f = ROOT.TFile.Open(str(path))
    if not f or f.IsZombie():
        raise SystemExit(f'analyzer-dev: cannot open {path}')
    out = {}

    def walk(d, prefix):
        for key in d.GetListOfKeys():
            obj = key.ReadObj()
            name = prefix + key.GetName()
            if obj.InheritsFrom('TDirectory'):
                walk(obj, name + '/')
            elif obj.InheritsFrom('TH1'):
                obj.SetDirectory(0)
                out[name] = obj
            elif obj.InheritsFrom('TNamed'):
                out[name] = obj.GetTitle()
    walk(f, '')
    f.Close()
    return out


def compare_root(fa, fb, tolerance):
    a, b = root_histograms(fa), root_histograms(fb)
    diffs = []
    for k in sorted(set(a) | set(b)):
        if k not in a or k not in b:
            diffs.append(f'{k}: only in {"A" if k in a else "B"}')
            continue
        ha, hb = a[k], b[k]
        if isinstance(ha, str) or isinstance(hb, str):
            if ha != hb:
                diffs.append(f'{k}: parameter files differ' if k == 'e88_parameters' else f'{k}: text differs')
            continue
        n = ha.GetNcells()
        if n != hb.GetNcells():
            diffs.append(f'{k}: binning differs ({n} -> {hb.GetNcells()} cells)')
            continue
        bad = [i for i in range(n) if not close(ha.GetBinContent(i), hb.GetBinContent(i), tolerance)]
        if bad or not close(ha.GetEntries(), hb.GetEntries(), tolerance):
            diffs.append(f'{k}: entries {ha.GetEntries():.0f} -> {hb.GetEntries():.0f}, '
                         f'integral {ha.Integral():g} -> {hb.Integral():g}, {len(bad)} of {n} cells differ')
    return diffs


def cmd_compare(a):
    for f in (a.a, a.b):
        if not Path(f).is_file():
            raise SystemExit(f'analyzer-dev: no file {f}')
    kinds = {Path(f).suffix for f in (a.a, a.b)}
    if kinds == {'.json'}:
        diffs = compare_json(a.a, a.b, a.tolerance)
    elif kinds == {'.root'}:
        diffs = compare_root(a.a, a.b, a.tolerance)
    else:
        raise SystemExit('analyzer-dev: compare two .root files or two .json files')
    print(f'A: {a.a}\nB: {a.b}')
    for d in diffs:
        print('  ' + d)
    print(f'{len(diffs)} difference(s)' if diffs else 'same')
    return 1 if diffs else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='action', required=True)
    r = sub.add_parser('run', help='analyze run files with your build')
    r.add_argument('--exe', default=str(REPO / 'build/bin/nda-replay'),
                   help='nda-replay of your build (default: build/bin/nda-replay of this repository)')
    r.add_argument('--data-dir', required=True, help='directory of the run files (only read)')
    which = r.add_mutually_exclusive_group(required=True)
    which.add_argument('--run', type=int, help='run number to analyze')
    which.add_argument('--latest', action='store_true', help='the most recently written run file')
    which.add_argument('--follow-new', action='store_true',
                       help='the newest run file, then every newer one, until Ctrl-C')
    r.add_argument('--out', required=True, help='new directory for the results, logs and report.json')
    r.add_argument('--http', help='also serve the histograms on this loopback ADDR:PORT')
    r.add_argument('--param', help='parameter index of the E88 monitor (--param of nda-replay)')
    r.add_argument('--idle-timeout', type=float, default=0,
                   help='stop a run after S seconds without new data (default: wait for the FileSink trailer)')
    r.add_argument('--nice', type=int, default=10, help='CPU niceness of the analyzer (default 10)')
    r.add_argument('--poll', type=float, default=2.0, help='--follow-new: look for a newer run file every S seconds')
    r.add_argument('extra', nargs='*', help='more nda-replay options, after --')
    c = sub.add_parser('compare', help='compare two results of the same run')
    c.add_argument('a')
    c.add_argument('b')
    c.add_argument('--tolerance', type=float, default=0.0, help='relative tolerance (default 0: exactly equal)')
    a = p.parse_args(argv)
    return cmd_run(a) if a.action == 'run' else cmd_compare(a)


if __name__ == '__main__':
    sys.exit(main())
