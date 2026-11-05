#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that Sugo offers the same features on Linux and macOS (ctest tst_platform_parity).

It runs the program in the smoke mode "inventory", which goes through every screen and lists
what the user can see and use there (src/qml/components/FeatureInventory.qml). The list is
compared with tests/platform-parity.json:

- an entry of "common" missing here means a feature that is gone on this platform;
- an entry found here that the manifest does not expect means a feature that exists on this
  platform only. If it belongs on both, add it to "common" (and check the other platform has
  it too). If it is truly platform-only, add it under "linux" or "macos" with the reason.

It also checks the platform's capabilities, and that every platform feature listed under
"tests" has its tests registered on this platform.

usage: check_platform_parity.py <sugo binary> <platform-parity.json> <file listing the registered tests>
       check_platform_parity.py --inventory <inventory.json> <platform-parity.json> [<tests file>]
       check_platform_parity.py --dump <sugo binary> <inventory.json>
       check_platform_parity.py --report <platform-parity.json> <directory> "<screens>"
The last two are for tests/compare_platforms.sh.
"""
import html
import json
import os
import subprocess
import sys
import tempfile


def run_inventory(app):
    with tempfile.TemporaryDirectory(prefix="sugo-parity.") as tmp:
        data = os.path.join(tmp, "data")
        os.makedirs(data)
        output = os.path.join(tmp, "inventory.json")
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": tmp,
            "QT_QPA_PLATFORM": "offscreen",
            "SUGO_SMOKE_PAGE": "inventory",
            "SUGO_INVENTORY": output,
            # Isolated data; no session bus, so nothing installed on the machine is reached.
            "SUGO_DATA_DIR": data,
            "XDG_DATA_HOME": os.path.join(tmp, "xdg-data"),
            "XDG_CONFIG_HOME": os.path.join(tmp, "xdg-config"),
            "XDG_CACHE_HOME": os.path.join(tmp, "xdg-cache"),
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/dev/null",
        }
        for name in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QT_PLUGIN_PATH"):
            if name in os.environ:
                env[name] = os.environ[name]
        try:
            result = subprocess.run([app], env=env, timeout=60, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        except subprocess.TimeoutExpired:
            sys.exit("The inventory run did not finish within 60 seconds")
        if result.returncode != 0 or not os.path.exists(output):
            sys.stderr.write(result.stdout)
            sys.exit("The inventory run failed (exit status %d)" % result.returncode)
        with open(output, encoding="utf-8") as file:
            return json.load(file)


def check(inventory, manifest, registered):
    platform = inventory["platform"]
    other = "macos" if platform == "linux" else "linux"
    problems = []

    for name, expected in manifest["capabilities"].items():
        got = inventory["capabilities"].get(name)
        if got != expected[platform]:
            problems.append("capability %s: %r, the manifest expects %r" % (name, got, expected[platform]))

    names = manifest.get("macosNames", {}) if platform == "macos" else {}
    updates = inventory["capabilities"].get("updateInstall", False)
    screens = manifest["screens"]
    for screen in sorted(set(screens) | set(inventory["screens"])):
        if screen not in screens:
            problems.append("%s: a screen the manifest does not know" % screen)
            continue
        if screen not in inventory["screens"]:
            problems.append("%s: the inventory did not reach this screen" % screen)
            continue
        spec = screens[screen]
        renames = names.get(screen, {})
        got = {renames.get(entry, entry) for entry in inventory["screens"][screen]}
        expected = set(spec["common"]) | set(spec.get(platform, {}))
        if updates:
            expected |= set(spec.get("updateInstall", []))
        allowed = expected | set(spec.get("updateInstall", []))
        for entry in sorted(expected - got):
            where = "common" if entry in spec["common"] else platform
            problems.append("%s: missing \"%s\" (listed under %s)" % (screen, entry, where))
        for entry in sorted(got - allowed):
            if entry in spec.get(other, {}):
                note = "the manifest has it on %s only" % other
            else:
                note = "not in the manifest"
            problems.append("%s: unexpected \"%s\" (%s)" % (screen, entry, note))

    if registered is not None:
        for feature, platforms in manifest.get("tests", {}).items():
            if feature.startswith("_"):
                continue
            for test in platforms.get(platform, []):
                if test not in registered:
                    problems.append("tests of \"%s\": %s is not registered on %s" % (feature, test, platform))

    return platform, problems


def read_tests(path):
    with open(path, encoding="utf-8") as file:
        return {line.strip() for line in file if line.strip()}


# The screenshots' start pages (SUGO_START_PAGE) and the inventory's screens.
SCREEN_OF_PAGE = {"presets": "timer-page", "about": "settings/about"}


def report(manifest_path, directory, pages):
    """index.html in `directory`: <platform>/<page>.png side by side, and what differs."""
    with open(manifest_path, encoding="utf-8") as file:
        manifest = json.load(file)
    inventories = {}
    for platform in ("linux", "macos"):
        path = os.path.join(directory, platform, "inventory.json")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as file:
                inventories[platform] = json.load(file)
    esc = html.escape
    parts = ["<!doctype html><meta charset=utf-8><title>Sugo: Linux and macOS</title>",
             "<style>body{font:14px system-ui;background:#111;color:#ddd;margin:24px}h2{margin-top:40px}"
             ".row{display:grid;grid-template-columns:1fr 1fr;gap:16px}img{width:100%;border:1px solid #333}"
             ".ok{color:#7c7}.bad{color:#f66}.note{color:#999}li{margin:2px 0}code{color:#fc6}</style>",
             "<h1>Sugo on Linux and macOS</h1>"]
    for platform, inventory in inventories.items():
        name, problems = check(inventory, manifest, None)
        if problems:
            parts.append("<p class=bad>%s: %d undeclared difference(s)</p><ul>%s</ul>" % (
                esc(name), len(problems), "".join("<li>%s</li>" % esc(p) for p in problems)))
        else:
            parts.append("<p class=ok>%s matches the manifest: every difference is declared.</p>" % esc(name))
    for page in pages.split():
        screen = SCREEN_OF_PAGE.get(page, page)
        image = page.replace("/", "-") + ".png"
        parts.append("<h2>%s</h2><div class=row>" % esc(screen))
        for platform in ("linux", "macos"):
            if os.path.exists(os.path.join(directory, platform, image)):
                parts.append("<div><b>%s</b><br><img src='%s/%s'></div>" % (platform, platform, image))
            else:
                parts.append("<div><b>%s</b><p class=note>no screenshot</p></div>" % platform)
        parts.append("</div>")
        spec = manifest["screens"].get(screen, {})
        notes = []
        for platform in ("linux", "macos"):
            for entry, reason in spec.get(platform, {}).items():
                notes.append("<li><code>%s</code> only on %s: %s</li>" % (esc(entry), platform, esc(reason)))
        for entry in spec.get("updateInstall", []):
            notes.append("<li><code>%s</code>: only in builds that install updates</li>" % esc(entry))
        for macos, linux in manifest.get("macosNames", {}).get(screen, {}).items():
            notes.append("<li class=note>macOS says <code>%s</code> for <code>%s</code></li>" % (esc(macos), esc(linux)))
        parts.append("<p>%d entries on both.</p><ul>%s</ul>" % (len(spec.get("common", [])), "".join(notes)))
    with open(os.path.join(directory, "index.html"), "w", encoding="utf-8") as file:
        file.write("\n".join(parts))
    return 0


def main(argv):
    if len(argv) == 4 and argv[1] == "--dump":
        with open(argv[3], "w", encoding="utf-8") as file:
            json.dump(run_inventory(argv[2]), file, indent=1)
        return 0
    if len(argv) == 5 and argv[1] == "--report":
        return report(argv[2], argv[3], argv[4])
    if len(argv) >= 3 and argv[1] == "--inventory":
        with open(argv[2], encoding="utf-8") as file:
            inventory = json.load(file)
        manifest_path = argv[3]
        registered = read_tests(argv[4]) if len(argv) > 4 else None
    elif len(argv) == 4:
        inventory = run_inventory(argv[1])
        manifest_path = argv[2]
        registered = read_tests(argv[3])
    else:
        sys.exit(__doc__)
    with open(manifest_path, encoding="utf-8") as file:
        manifest = json.load(file)
    platform, problems = check(inventory, manifest, registered)
    if problems:
        print("Sugo on %s does not match %s:" % (platform, os.path.basename(manifest_path)))
        for problem in problems:
            print("  - " + problem)
        print("A feature on one platform only must be listed there with the reason (see the comment at its top).")
        return 1
    count = sum(len(entries) for entries in inventory["screens"].values())
    print("Sugo on %s matches the manifest: %d screens, %d entries" % (platform, len(inventory["screens"]), count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
