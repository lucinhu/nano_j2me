#!/usr/bin/env python3
"""Compare real Random subclass bytecode with the desktop JVM."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    'methods', 'inherited-super-next', 'rejection-invalid-power2', 'signed-long',
    'constructor-set-seed', 'constructor-throws', 'next-throws-short-circuit',
    'gaussian-double-cache-reset', 'gaussian-throws-no-cache', 'gaussian-super-double',
}


def run(argv, work, name):
    result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    (work / (name + '.log')).write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('{} exited {}\n{}'.format(name, result.returncode, result.stdout[-4000:]))
    return result.stdout


def parse(text):
    rows = re.findall(r'^RANDOM-DISPATCH ([a-z0-9-]+) (-?\d+)\s*$', text, re.M)
    result = dict(rows)
    if len(rows) != len(CASES) or set(result) != CASES or 'RANDOM-DISPATCH DONE' not in text:
        raise RuntimeError('incomplete or duplicate Random dispatch results\n' + text[-4000:])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, help='retain logs in a new directory under .local-tools/debug-runs')
    args = parser.parse_args()
    for tool in ('java', 'javac'):
        if not shutil.which(tool):
            print('SKIP: {} is unavailable'.format(tool))
            return 77
    parent = ROOT / '.local-tools' / 'debug-runs'
    parent.mkdir(parents=True, exist_ok=True)
    if args.output:
        work = args.output.resolve()
        try:
            relative = work.relative_to(parent.resolve())
        except ValueError:
            raise RuntimeError('--output must be under ' + str(parent))
        if relative == Path('.'):
            raise RuntimeError('--output must name a new scratch directory')
        work.mkdir(parents=True, exist_ok=False)
    else:
        work = Path(tempfile.mkdtemp(prefix='random-dispatch-java-', dir=parent)).resolve()
    try:
        binary = args.binary.resolve(strict=True)
        classes = work / 'classes'
        classes.mkdir()
        source = work / 'Launch.java'
        source.write_text('''package randomdispatch;
import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Display;
public class Launch extends MIDlet {
    static class Blank extends Canvas { protected void paint(Graphics g) { } }
    public void startApp() {
        Display.getDisplay(this).setCurrent(new Blank());
        RandomDispatch.main(null);
        notifyDestroyed();
    }
    public void pauseApp() { }
    public void destroyApp(boolean unconditional) { }
}
''', encoding='utf-8')
        cp = run([sys.executable, str(ROOT / 'tools/midp_stubs.py'), '--quiet', '--out', str(work / 'stubs')], work, 'stubs').strip()
        run(['javac', '-source', '8', '-target', '8', '-nowarn', '-cp', cp,
             '-d', str(classes), str(ROOT / 'tests/java/RandomDispatch.java'), str(source)], work, 'javac')
        desktop = run(['java', '-cp', str(classes), 'randomdispatch.RandomDispatch'], work, 'desktop')
        expected = parse(desktop)
        archive = work / 'random-dispatch.jar'
        manifest = ('Manifest-Version: 1.0\nMIDlet-Name: RandomDispatch\nMIDlet-Version: 1.0.0\n'
                    'MIDlet-Vendor: nano_j2me\nMIDlet-1: RandomDispatch, , randomdispatch.Launch\n\n')
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as jar:
            jar.writestr('META-INF/MANIFEST.MF', manifest)
            for item in sorted(classes.rglob('*.class')):
                jar.write(item, item.relative_to(classes).as_posix())
        interp = run([str(binary), '--port', 'headless', '--heap', '8', '--size', '240x320',
                      '--fps', '30', '--run-frames', '30', '--rms-dir', str(work / 'rms'),
                      '--fs-dir', str(work / 'fs'), str(archive)], work, 'interpreter')
        actual = parse(interp)
        mismatches = {key: {'desktop': expected[key], 'emulator': actual[key]}
                      for key in sorted(CASES) if expected[key] != actual[key]}
        (work / 'results.json').write_text(json.dumps({'expected': expected, 'actual': actual,
                                                     'mismatches': mismatches}, indent=2) + '\n')
        if mismatches:
            raise RuntimeError('Random subclass mismatch:\n' + json.dumps(mismatches, indent=2))
        if re.search(r'uncaught exception', interp, re.I):
            raise RuntimeError('uncaught exception in emulator output')
        print('PASS: {} Random subclass categories match desktop Java'.format(len(CASES)))
        return 0
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print('FAIL: ' + str(exc), file=sys.stderr)
        return 1
    finally:
        if args.output:
            print('Logs: ' + str(work))
        else:
            # Only remove the unique scratch directory created above.
            assert work.resolve() == work and work.parent == parent.resolve()
            assert work.name.startswith('random-dispatch-java-')
            shutil.rmtree(work)


if __name__ == '__main__':
    sys.exit(main())
