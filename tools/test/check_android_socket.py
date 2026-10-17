#!/usr/bin/env python3
"""Run real PlatformSocket loopback tests with a JDK, optionally on Android.

Usage: python3 tools/test/check_android_socket.py [--device SERIAL]
Android requires --sdk PATH or ANDROID_SDK_ROOT/ANDROID_HOME (~/Android/Sdk
is the default). Stubs, classes and run logs stay under .local-tools.
"""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def run(command, log):
    result = subprocess.run([str(x) for x in command], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=90)
    print(result.stdout, end="")
    log.write(result.stdout)
    log.flush()
    result.check_returncode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device")
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_SDK_ROOT")
                        or os.environ.get("ANDROID_HOME") or str(Path.home() / "Android/Sdk"))
    args = parser.parse_args()
    parent = ROOT / ".local-tools/debug-runs"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="android-socket-", dir=parent))
    classes = out / "classes"
    classes.mkdir()
    source = ROOT / "src/port/android/java/com/lucinhu/nj2me/PlatformSocket.java"
    test = ROOT / "tools/test/PlatformSocketTest.java"
    with (out / "run.log").open("w") as log:
        if args.device:
            sdk = Path(args.sdk)
            jars = sorted((sdk / "platforms").glob("*/android.jar"))
            tools = sorted((sdk / "build-tools").glob("*/d8"))
            if not jars or not tools:
                raise SystemExit("Android SDK android.jar and d8 are required")
            run(["javac", "--release", "8", "-cp", jars[-1], "-d", classes, source, test], log)
            classjar = out / "classes.jar"
            with zipfile.ZipFile(classjar, "w") as archive:
                for path in sorted(classes.rglob("*.class")):
                    archive.write(path, path.relative_to(classes))
            dex = out / "dex"
            dex.mkdir()
            run([tools[-1], "--min-api", "26", "--lib", jars[-1], "--output", dex, classjar], log)
            dexjar = out / "socket-test.jar"
            with zipfile.ZipFile(dexjar, "w") as archive:
                archive.write(dex / "classes.dex", "classes.dex")
            remote = "/data/local/tmp/nj-socket-" + out.name + ".jar"
            adb = ["adb", "-s", args.device]
            try:
                run(adb + ["push", dexjar, remote], log)
                run(adb + ["shell", "env", "CLASSPATH=" + remote, "app_process",
                           "/system/bin", "com.lucinhu.nj2me.PlatformSocketTest"], log)
            finally:
                # Only remove the exact unique path this run created.
                run(adb + ["shell", "rm", "-f", remote], log)
        else:
            context = out / "stubs/android/content/Context.java"
            context.parent.mkdir(parents=True)
            context.write_text("package android.content; public class Context {}\n")
            androidlog = out / "stubs/android/util/Log.java"
            androidlog.parent.mkdir(parents=True)
            androidlog.write_text("package android.util; public class Log {"
                                  "public static int i(String tag,String msg){return 0;}"
                                  "public static int w(String tag,String msg){return 0;}}\n")
            run(["javac", "--release", "8", "-d", classes, context, androidlog, source, test], log)
            run(["java", "-cp", classes, "com.lucinhu.nj2me.PlatformSocketTest"], log)
    print("Evidence:", out / "run.log")


if __name__ == "__main__":
    main()
