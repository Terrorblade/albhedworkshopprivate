"""Boot FFX, give it a while, then kill it and report what the logs say.

There is no debugger on this machine and no automated way to drive the game's
menus, but almost every failure this project has hit shows up in the logs or in
albhed_crash.log within the first minute: a crash report, a hang watchdog report,
a plugin that refused to hook, or a list rebuild that takes seconds.

    python tools/boottest.py                 # 60 seconds, then report
    python tools/boottest.py --seconds 90
    python tools/boottest.py --clean-cache    # delete the export cache first

It is deliberately not clever. It truncates the logs so what it reads belongs to
this run, starts the exe, waits, kills it, and then says PASS or FAIL with the
lines that decided it.
"""

import argparse
import os
import re
import subprocess
import sys
import time

GAME_DIR = r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
EXE = os.path.join(GAME_DIR, "FFX.exe")
WORKSHOP = os.path.join(GAME_DIR, "AlBhedWorkshop")

LOGS = ["workshop_loader.log", "albhed_cheats.log", "pilgrimage_together.log",
        "albhed_crash.log"]

# Lines that mean the run failed, as (pattern, why) pairs.
BAD = [
    (r"^CRASH\s", "a crash report was written"),
    (r"HAS NOT STEPPED FOR", "the hang watchdog caught a freeze"),
    (r"LAYOUT CHECK FAILED", "a plugin refused to hook"),
    (r"COULD NOT BE INSTALLED", "the crash handler could not install"),
    (r"could not be written", "a write failed"),
    (r"never published a swapchain", "the overlay never found the swapchain"),
    (r"0 loaded", "no plugins loaded"),
    (r"did not fill", "a table never filled"),
]

# Lines that mean it got far enough to be worth calling a pass.
GOOD = [
    (r"plugin scan done: (\d+) loaded, 0 failed", "plugins loaded"),
    (r"layout check passed", "the layout check passed"),
    (r"ready\.", "a plugin finished starting"),
]


def truncate_logs():
    for name in LOGS:
        path = os.path.join(WORKSHOP, name)
        if os.path.exists(path):
            try:
                open(path, "w").close()
            except OSError as e:
                print("could not truncate %s: %s" % (name, e))


def clean_cache():
    data = os.path.join(WORKSHOP, "data")
    if not os.path.isdir(data):
        print("no export cache to clean")
        return
    gone = 0
    for name in os.listdir(data):
        if name.endswith(".tsv"):
            os.remove(os.path.join(data, name))
            gone += 1
    print("deleted %d cache files" % gone)


def save_crash_log(run_index):
    """Copies albhed_crash.log aside, because truncate_logs on the next run would
    otherwise throw away the evidence of the run that died."""
    src = os.path.join(WORKSHOP, "albhed_crash.log")
    if not os.path.exists(src):
        return
    dst = os.path.join(WORKSHOP, "albhed_crash.run%d.log" % run_index)
    try:
        with open(src, "rb") as f:
            data = f.read()
        with open(dst, "wb") as f:
            f.write(data)
        print("  crash report kept as %s" % os.path.basename(dst))
    except OSError as e:
        print("  could not keep the crash report: %s" % e)


def read_log(name):
    path = os.path.join(WORKSHOP, name)
    if not os.path.exists(path):
        return []
    with open(path, "r", errors="replace") as f:
        return f.read().splitlines()


def timestamps(lines):
    """The first and last bracketed timestamp, for a rough idea of progress."""
    stamps = []
    for line in lines:
        m = re.match(r"\[(\d\d:\d\d:\d\d\.\d+)", line)
        if m:
            stamps.append(m.group(1))
    return (stamps[0], stamps[-1]) if stamps else (None, None)


def ffx_alive():
    """True while any FFX.exe is running. tasklist says so without a kill."""
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq FFX.exe", "/NH"],
                         capture_output=True, text=True).stdout
    return "FFX.exe" in out


def run(seconds):
    """Returns the second at which every FFX.exe went away, or None if it survived.

    This is the only failure signal in a control run with no plugins, because
    without a plugin there is no crash handler and nothing writes a log.
    """
    print("starting %s" % EXE)
    proc = subprocess.Popen([EXE], cwd=GAME_DIR)

    # The game relaunches itself through Steam sometimes, so the pid we started is
    # not necessarily the one that lives. Watch for any FFX.exe instead.
    died_at = None
    for i in range(seconds):
        time.sleep(1)
        if died_at is None and not ffx_alive():
            died_at = i + 1
            print("  FFX.exe is gone after %ds" % died_at)
            break
        if i % 10 == 9:
            print("  %ds" % (i + 1))

    if died_at is None:
        print("still running at %ds, killing every FFX.exe" % seconds)
    subprocess.call(["taskkill", "/F", "/IM", "FFX.exe"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=10)
    except Exception:
        pass
    time.sleep(1)
    return died_at


def report(died_at=None):
    failures = []
    if died_at is not None:
        failures.append(("process", "the game exited on its own",
                         "every FFX.exe was gone after %ds" % died_at))
    passes = []

    for name in LOGS:
        lines = read_log(name)
        if not lines:
            continue
        first, last = timestamps(lines)
        span = ""
        if first and last:
            span = "  %s -> %s" % (first, last)
        print("\n--- %s, %d lines%s" % (name, len(lines), span))

        for line in lines:
            for pattern, why in BAD:
                if re.search(pattern, line):
                    failures.append((name, why, line.strip()))
            for pattern, why in GOOD:
                if re.search(pattern, line):
                    passes.append((name, why, line.strip()))

    print("\n================ result ================")
    for name, why, line in passes:
        print("  ok    %-24s %s" % (why, line[:110]))

    if not failures:
        if not passes:
            print("\nFAIL: nothing recognisable in the logs at all. Did the game start?")
            return 1
        print("\nPASS: no crash, no freeze, no refused hook.")
        return 0

    print("")
    for name, why, line in failures:
        print("  FAIL  %-24s %s" % (why, line[:110]))
    print("\nFAIL: %d problem line(s). Read %s for the detail."
          % (len(failures), os.path.join(WORKSHOP, "albhed_crash.log")))
    return 1


def keep_only(which):
    """Moves every plugin DLL aside except the ones whose name contains `which`,
    and returns a function that puts them back.

    This is how a crash gets bisected between plugins without a rebuild. Both
    plugins install the overlay and the crash handler, so either one alone still
    reports.
    """
    live = os.path.join(WORKSHOP, "plugins")
    aside = os.path.join(WORKSHOP, "plugins.boottest-aside")
    if not os.path.isdir(live):
        print("no plugins directory")
        return None
    if os.path.isdir(aside):
        print("%s already exists, refusing to clobber it" % aside)
        return None
    os.makedirs(aside)

    moved = []
    kept = []
    for name in os.listdir(live):
        if not name.lower().endswith(".dll"):
            continue
        if which.lower() in name.lower():
            kept.append(name)
            continue
        os.rename(os.path.join(live, name), os.path.join(aside, name))
        moved.append(name)

    if not kept:
        for name in moved:
            os.rename(os.path.join(aside, name), os.path.join(live, name))
        os.rmdir(aside)
        print("nothing matches %r, so there would be no plugin left" % which)
        return None

    print("keeping %s, moved aside %s" % (", ".join(kept), ", ".join(moved) or "nothing"))

    def restore():
        for name in moved:
            src = os.path.join(aside, name)
            if os.path.exists(src):
                os.rename(src, os.path.join(live, name))
        try:
            os.rmdir(aside)
        except OSError:
            pass
        if moved:
            print("plugins put back")
    return restore


def move_plugins_aside():
    """Renames the plugins directory so the loader finds nothing, and returns a
    function that puts it back. Used by --no-plugins to get a control run."""
    live = os.path.join(WORKSHOP, "plugins")
    aside = os.path.join(WORKSHOP, "plugins.boottest-aside")
    if not os.path.isdir(live):
        print("no plugins directory to move")
        return lambda: None
    if os.path.isdir(aside):
        print("%s already exists, refusing to clobber it" % aside)
        return None
    os.rename(live, aside)
    os.makedirs(live)
    print("plugins moved aside, this is a control run with nothing of ours loaded")

    def restore():
        try:
            os.rmdir(live)
        except OSError:
            pass
        if os.path.isdir(aside) and not os.path.isdir(live):
            os.rename(aside, live)
            print("plugins put back")
    return restore


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=60,
                    help="how long to let it run before killing it")
    ap.add_argument("--clean-cache", action="store_true",
                    help="delete the exported list cache first, to test a cold build")
    ap.add_argument("--no-run", action="store_true",
                    help="only read the logs that are already there")
    ap.add_argument("--only", metavar="SUBSTRING",
                    help="bisect: keep only the plugin DLLs whose filename "
                         "contains this, moving the rest aside for the run")
    ap.add_argument("--runs", type=int, default=1,
                    help="repeat the boot this many times. The crash being chased "
                         "is intermittent, so one run proves nothing.")
    ap.add_argument("--no-plugins", action="store_true",
                    help="control run: move the plugins aside so nothing of ours "
                         "loads. The only signal is whether the game stays alive, "
                         "because without a plugin nothing writes a log.")
    a = ap.parse_args(argv[1:])

    if not os.path.exists(EXE):
        print("no FFX.exe at %s" % EXE)
        return 2

    restore = None
    if a.no_plugins and a.only:
        print("--no-plugins and --only contradict each other")
        return 2
    if a.no_plugins:
        restore = move_plugins_aside()
        if restore is None:
            return 2
    elif a.only:
        restore = keep_only(a.only)
        if restore is None:
            return 2

    if a.clean_cache:
        clean_cache()

    died_at = None
    if a.runs > 1:
        label = a.only or ("no plugins" if a.no_plugins else "everything")
        deaths = []
        try:
            for i in range(a.runs):
                print("\n=========== run %d of %d, %s ===========" % (i + 1, a.runs, label))
                # Per run, not once, so --clean-cache with --runs N stresses the
                # cold path N times instead of only the first time.
                if a.clean_cache:
                    clean_cache()
                truncate_logs()
                d = run(a.seconds)
                if d is not None:
                    deaths.append((i + 1, d))
                    # Keep the report of the run that died, it is the evidence.
                    save_crash_log(i + 1)
        finally:
            if restore:
                restore()

        print("\n================ %d runs, %s ================" % (a.runs, label))
        if not deaths:
            print("PASS: every run stayed up for %ds." % a.seconds)
            return 0
        for i, d in deaths:
            print("  run %d died after %ds" % (i, d))
        print("\nFAIL: %d of %d runs died." % (len(deaths), a.runs))
        return 1

    try:
        if not a.no_run:
            truncate_logs()
            died_at = run(a.seconds)
    finally:
        if restore:
            restore()

    if a.no_plugins:
        # No logs to read, so the answer is just whether it lived.
        print("\n================ result ================")
        if died_at is None:
            print("\nPASS: vanilla FFX stayed up for %ds. The crash needs our code "
                  "loaded, so it is ours." % a.seconds)
            return 0
        print("\nFAIL: vanilla FFX died after %ds with no plugins loaded at all. "
              "The crash is the game's own." % died_at)
        return 1

    return report(died_at)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
