"""One command: all headless Lua suites + the UI self-test.   python tests/run_all.py [--no-ui]
Prints a per-suite PASS/FAIL line and exits non-zero if anything failed."""
import os, re, subprocess, sys, tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(REPO, "build", "bin", "aseprite.exe")
SUITES = ["easymetric_test.lua", "easymetric_adversarial_stress.lua", "easymetric_four_requirements_test.lua",
          "easymetric_texture_layer_panel_bugs.lua", "easymetric_depth_sculpt_test.lua",
          "easymetric_studio_tools_test.lua"]
failed = False
user = tempfile.mkdtemp(prefix="asehl_")
os.makedirs(os.path.join(user, "extensions", "pixellab"), exist_ok=True)
env = dict(os.environ, ASEPRITE_USER_FOLDER=user)
for s in SUITES:
    path = os.path.join(REPO, "tests", s)
    if not os.path.exists(path):
        continue
    p = subprocess.run([EXE, "-b", "--script", path], capture_output=True, text=True, env=env, cwd=REPO)
    out = p.stdout + p.stderr
    m = re.search(r"TOTAL:\s*(\d+) Passed,\s*(\d+) Failed", out) or re.search(r"(\d+) passed.*?(\d+) failed", out, re.I)
    passed, bad = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    ok = bad == 0
    failed |= not ok
    print("%s %-45s %s passed, %s failed (exit %s)" % ("PASS" if ok else "FAIL", s, passed, bad, p.returncode))
    if not ok:
        print(out[-1500:])
if "--no-ui" not in sys.argv:
    ui_dir = os.path.join(REPO, "tests", "ui")
    for name in sorted(os.listdir(ui_dir)):
        if not name.endswith(".py"):
            continue
        p = subprocess.run([sys.executable, os.path.join(ui_dir, name)], text=True, capture_output=True)
        last = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else "no output"
        print("%s %-45s %s" % ("PASS" if p.returncode == 0 else "FAIL", "ui/" + name, last))
        failed |= p.returncode != 0
        if p.returncode != 0:
            print(p.stdout[-2500:], p.stderr[-800:])
sys.exit(1 if failed else 0)
