import importlib.util
import io
import json
import os
import signal
import socket
import stat
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'analyzer_dev', Path(__file__).resolve().parent / 'analyzer-dev.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

# A stand-in for nda-replay: writes the --output and --summary-json files and exits.
FAKE_REPLAY = '''#!/usr/bin/env python3
import json, sys, time
a = sys.argv[1:]
def opt(k):
    return a[a.index(k) + 1] if k in a else None
run = int(opt('--input').rsplit('run', 1)[1][:6])
open(opt('--output'), 'w').write('root')
json.dump({'run_number': run, 'time_frames': 10, 'param': opt('--param')}, open(opt('--summary-json'), 'w'))
print('args', ' '.join(a))
'''


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.data = self.d / 'data'
        self.data.mkdir()
        self.exe = self.d / 'nda-replay'
        self.exe.write_text(FAKE_REPLAY)
        self.exe.chmod(self.exe.stat().st_mode | stat.S_IXUSR)

    def tearDown(self):
        self.tmp.cleanup()

    def write_run(self, run, mtime=None, size=8):
        p = self.data / f'run{run:06d}.dat'
        p.write_bytes(b'x' * size)
        if mtime is not None:
            os.utime(p, (mtime, mtime))
        return p

    def main(self, *argv):
        with redirect_stdout(io.StringIO()) as out:
            try:
                rc = m.main(list(argv))
            except SystemExit as exc:
                rc = exc.code
        return rc, out.getvalue()


class RunFilesTests(Base):
    def test_newest_by_mtime_and_skips_empty(self):
        self.write_run(5, mtime=1000)
        self.write_run(3, mtime=2000)
        self.write_run(9, mtime=3000, size=0)
        (self.data / 'run12.dat').write_bytes(b'x')
        self.assertEqual(m.newest_run(self.data), 3)
        self.assertEqual(sorted(m.run_files(self.data)), [3, 5, 9])

    def test_no_files(self):
        self.assertIsNone(m.newest_run(self.data))


class RunTests(Base):
    def test_single_run_writes_results_and_report(self):
        self.write_run(16)
        out = self.d / 'out'
        rc, _ = self.main('run', '--exe', str(self.exe), '--data-dir', str(self.data), '--run', '16', '--out', str(out),
                          '--nice', '0', '--', '--max-frames', '5')
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads((out / 'run000016.json').read_text())['run_number'], 16)
        log = (out / 'analyzer-run000016.log').read_text()
        self.assertIn('--follow', log)
        self.assertIn('--max-frames 5', log)
        self.assertNotIn('--http', log)
        report = json.loads((out / 'report.json').read_text())
        self.assertEqual(report['mode'], 'single')
        self.assertEqual(report['runs'][0]['returncode'], 0)
        self.assertEqual(report['analyzer']['sha256'], m.hashlib.sha256(self.exe.read_bytes()).hexdigest())

    def test_restart_from_the_page_runs_the_analyzer_again(self):
        self.write_run(3)
        marker = self.d / 'restarted'
        self.exe.write_text(FAKE_REPLAY.replace(
            "print('args'", f"import os\nif not os.path.exists({str(marker)!r}):\n    open({str(marker)!r}, 'w').close()\n"
            "    sys.exit(75)\nprint('args'"))
        out = self.d / 'out'
        rc, _ = self.main('run', '--exe', str(self.exe), '--data-dir', str(self.data), '--run', '3', '--out', str(out),
                          '--nice', '0')
        self.assertEqual(rc, 0)
        report = json.loads((out / 'report.json').read_text())
        self.assertEqual(len(report['runs']), 1)
        self.assertEqual(len(report['runs'][0]['restarts']), 1)
        self.assertTrue((out / 'run000003.json').exists())

    def test_latest(self):
        self.write_run(7, mtime=1000)
        self.write_run(8, mtime=2000)
        out = self.d / 'out'
        rc, _ = self.main('run', '--exe', str(self.exe), '--data-dir', str(self.data), '--latest', '--out', str(out),
                          '--nice', '0')
        self.assertEqual(rc, 0)
        self.assertTrue((out / 'run000008.root').exists())

    def test_refusals(self):
        self.write_run(1)
        base = ['run', '--exe', str(self.exe), '--data-dir', str(self.data), '--run', '1']
        existing = self.d / 'exists'
        existing.mkdir()
        cases = {
            'new directory': ['--out', str(existing)],
            'inside the data directory': ['--out', str(self.data / 'x')],
            'loopback': ['--out', str(self.d / 'o1'), '--http', '0.0.0.0:18391'],
            'no run file': None,
        }
        for text, extra in cases.items():
            argv = base + extra if extra else ['run', '--exe', str(self.exe), '--data-dir', str(self.data), '--run',
                                               '2', '--out', str(self.d / 'o2')]
            rc, _ = self.main(*argv)
            self.assertIn(text, str(rc), text)
        self.assertFalse((self.data / 'x').exists())

    def test_port_in_use_is_refused(self):
        self.write_run(1)
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            s.listen()
            port = s.getsockname()[1]
            rc, _ = self.main('run', '--exe', str(self.exe), '--data-dir', str(self.data), '--run', '1', '--out',
                              str(self.d / 'o'), '--http', f'127.0.0.1:{port}')
        self.assertIn('in use', str(rc))
        self.assertFalse((self.d / 'o').exists())

    def test_follow_new_takes_newer_runs(self):
        self.write_run(16, mtime=time.time() - 100)
        out = self.d / 'out'
        result = {}

        def later():
            time.sleep(0.6)
            self.write_run(17)
            time.sleep(0.8)
            os.kill(os.getpid(), signal.SIGINT)
        t = threading.Thread(target=later)
        t.start()
        result['rc'], _ = self.main('run', '--exe', str(self.exe), '--data-dir', str(self.data), '--follow-new',
                                    '--out', str(out), '--nice', '0', '--poll', '0.1')
        t.join()
        self.assertEqual(result['rc'], 0)
        report = json.loads((out / 'report.json').read_text())
        self.assertEqual([r['run_number'] for r in report['runs']], [16, 17])
        self.assertTrue((out / 'run000017.json').exists())


class CompareTests(Base):
    def write(self, name, obj):
        p = self.d / name
        p.write_text(json.dumps(obj))
        return str(p)

    def test_same_and_ignored_fields(self):
        a = self.write('a.json', {'time_frames': 10, 'run_seconds': 1.0, 'modules': [{'id': 'm1', 'frames': 3}],
                                  'e88_parameters': {'files': [{'path': '/x', 'sha256': 'aa'}]}})
        b = self.write('b.json', {'time_frames': 10, 'run_seconds': 2.0, 'modules': [{'id': 'm1', 'frames': 3}],
                                  'e88_parameters': {'files': [{'path': '/y', 'sha256': 'aa'}]}})
        rc, out = self.main('compare', a, b)
        self.assertEqual(rc, 0)
        self.assertIn('same', out)

    def test_differences(self):
        a = self.write('a.json', {'time_frames': 10, 'modules': [{'id': 'm1', 'frames': 3}], 'old': 1})
        b = self.write('b.json', {'time_frames': 11, 'modules': [{'id': 'm1', 'frames': 4}], 'new': 1})
        rc, out = self.main('compare', a, b)
        self.assertEqual(rc, 1)
        for text in ('time_frames: 10 -> 11', 'modules.m1.frames: 3 -> 4', 'old: only in A', 'new: only in B'):
            self.assertIn(text, out)

    def test_tolerance(self):
        a = self.write('a.json', {'rate': 100.0})
        b = self.write('b.json', {'rate': 100.5})
        self.assertEqual(self.main('compare', a, b)[0], 1)
        self.assertEqual(self.main('compare', a, b, '--tolerance', '0.01')[0], 0)

    def test_mixed_kinds_refused(self):
        a = self.write('a.json', {})
        (self.d / 'b.root').write_text('')
        rc, _ = self.main('compare', a, str(self.d / 'b.root'))
        self.assertIn('two .root files', str(rc))


if __name__ == '__main__':
    unittest.main()
