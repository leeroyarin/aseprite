"""Compares the current build's Easymetric render hashes with tests/easymetric_golden.txt.
Run: python tests/check_golden.py            (exit 1 on any difference)
     python tests/check_golden.py --update   (rewrite the golden file after an intended change)"""
import os
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(REPO, "build", "bin", "aseprite.exe")
GOLDEN = os.path.join(REPO, "tests", "easymetric_golden.txt")


def run():
    user = tempfile.mkdtemp(prefix="asegold_")
    os.makedirs(os.path.join(user, "extensions", "pixellab"), exist_ok=True)
    env = dict(os.environ, ASEPRITE_USER_FOLDER=user)
    p = subprocess.run([EXE, "-b", "--script", os.path.join(REPO, "tests", "easymetric_golden.lua")],
                       capture_output=True, text=True, env=env, cwd=REPO)
    return [l for l in (p.stdout + p.stderr).splitlines() if l.startswith("GOLDEN ")]


def main():
    now = run()
    if "--update" in sys.argv:
        open(GOLDEN, "w").write("\n".join(now) + "\n")
        print("updated", len(now), "lines")
        return 0
    want = [l for l in open(GOLDEN).read().splitlines() if l.strip()]
    bad = 0
    for i, w in enumerate(want):
        n = now[i] if i < len(now) else "<missing>"
        ok = n == w
        bad += not ok
        print(("PASS " if ok else "FAIL ") + w.split()[1] + ("" if ok else "\n  want " + w + "\n  got  " + n))
    print("TOTAL: %d Passed, %d Failed" % (len(want) - bad, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
