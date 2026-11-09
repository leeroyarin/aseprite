"""Client for the in-app Aseprite UI test server (docs/aseprite2/UITEST.md).

Launches (a private copy of) aseprite.exe with ASEPRITE_UITEST_DIR set and an isolated
user folder, then drives it with real in-app mouse/keyboard events and reads structured
state back as JSON. Nothing touches the desktop mouse or keyboard.

    from uitest import UiTest
    with UiTest(files=["sprite.aseprite"]) as t:
        t.lua('app.command.EasymetricStudio()')       # open the studio
        st = t.state()["studio"]
        t.click_widget(text="Depth Brush")
        t.drag(100, 200, 160, 200)
        print(t.state()["faces"]["TOP"]["depth"])
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


class UiTestError(RuntimeError):
    pass


class UiTest:
    def __init__(self, files=(), exe=None, workdir=None, scale=None, keep=False,
                 fresh_user_folder=True, env=None, timeout=60):
        self.workdir = workdir or tempfile.mkdtemp(prefix="asetest_")
        self.keep = keep
        self.timeout = timeout
        self.cmd_dir = os.path.join(self.workdir, "ipc")
        os.makedirs(self.cmd_dir, exist_ok=True)
        # Private copy of the binary so a rebuild never hits a locked exe.
        src_bin = os.path.dirname(exe) if exe else os.path.join(REPO, "build", "bin")
        exe_name = os.path.basename(exe) if exe else "aseprite.exe"
        self.bin_dir = os.path.join(self.workdir, "bin")
        if not os.path.isdir(self.bin_dir):
            shutil.copytree(src_bin, self.bin_dir, ignore=shutil.ignore_patterns(
                "*.pdb", "*.ilk", "*.old", "allevents*", "base64*", "*.obj", "pixellab*"))
        self.exe = os.path.join(self.bin_dir, exe_name)
        self.user_dir = os.path.join(self.workdir, "userdir")
        if fresh_user_folder and os.path.isdir(self.user_dir):
            shutil.rmtree(self.user_dir, ignore_errors=True)
        os.makedirs(self.user_dir, exist_ok=True)
        # Aseprite 2.0 auto-imports the user's PixelLab extension into an empty user folder
        # (extensions.cpp autoImportPixelLab) and PixelLab then pops up dialogs. An existing
        # (empty) "pixellab" folder switches the import off, so tests start clean.
        os.makedirs(os.path.join(self.user_dir, "extensions", "pixellab"), exist_ok=True)
        e = dict(os.environ)
        e["ASEPRITE_USER_FOLDER"] = self.user_dir
        e["ASEPRITE_UITEST_DIR"] = self.cmd_dir
        if env:
            e.update(env)
        for stale in ("ready.json", "res.json", "cmd.json"):
            p = os.path.join(self.cmd_dir, stale)
            if os.path.exists(p):
                os.remove(p)
        args = [self.exe]
        if scale:
            args += ["--screen-scale", str(scale)]
        args += list(files)
        self.proc = subprocess.Popen(args, env=e, cwd=self.bin_dir)
        self._seq = 0
        self._wait_ready()

    # ------------------------------------------------------------------ plumbing
    def _wait_ready(self):
        ready = os.path.join(self.cmd_dir, "ready.json")
        t0 = time.time()
        while time.time() - t0 < self.timeout:
            if os.path.exists(ready):
                time.sleep(0.5)
                self.call("ping")
                return
            if self.proc.poll() is not None:
                raise UiTestError("aseprite exited early with code %s" % self.proc.returncode)
            time.sleep(0.1)
        raise UiTestError("UI test server never became ready")

    def call(self, op, **kw):
        self._seq += 1
        cmd = dict(kw)
        cmd["op"] = op
        cmd["id"] = self._seq
        tmp = os.path.join(self.cmd_dir, "cmd.tmp")
        res_path = os.path.join(self.cmd_dir, "res.json")
        if os.path.exists(res_path):
            os.remove(res_path)
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(cmd, f)
        os.replace(tmp, os.path.join(self.cmd_dir, "cmd.json"))
        t0 = time.time()
        while time.time() - t0 < self.timeout:
            if os.path.exists(res_path):
                try:
                    with open(res_path, "r", encoding="utf-8") as f:
                        res = json.load(f)
                except ValueError:
                    time.sleep(0.02)
                    continue
                if res.get("id") == self._seq:
                    if not res.get("ok", False):
                        raise UiTestError("%s failed: %s" % (op, res.get("error")))
                    return res
            if self.proc.poll() is not None:
                raise UiTestError("aseprite died during %s (exit %s)" % (op, self.proc.returncode))
            time.sleep(0.01)
        raise UiTestError("timeout waiting for %s" % op)

    # ------------------------------------------------------------------ reads
    def state(self):
        return self.call("state")["state"]

    def ui(self, all=False, maxDepth=40):
        return self.call("ui", all=all, maxDepth=maxDepth)

    def find(self, **sel):
        return self.call("find", **sel)["matches"]

    def cursor(self):
        return self.call("cursor")["cursor"]

    def lua(self, code):
        r = self.call("lua", code=code)
        if not r.get("luaOk", True):
            raise UiTestError("lua error: %s" % r.get("luaErrors"))
        return r.get("output", "")

    # ------------------------------------------------------------------ input
    def click(self, x, y, button="left", mods="", settle=60):
        return self.call("click", x=x, y=y, button=button, mods=mods, settle=settle)

    def double_click(self, x, y, button="left"):
        return self.call("double_click", x=x, y=y, button=button)

    def click_widget(self, button="left", mods="", nth=0, **sel):
        return self.call("click_widget", button=button, mods=mods, nth=nth, **sel)

    def move(self, x, y):
        return self.call("mouse_move", x=x, y=y)

    def drag(self, x1, y1, x2, y2, steps=8, button="left", mods="", via=None, stepMs=16):
        kw = dict(x1=x1, y1=y1, x2=x2, y2=y2, steps=steps, button=button, mods=mods, stepMs=stepMs)
        if via:
            kw["via"] = via
        return self.call("drag", **kw)

    def wheel(self, x, y, dy=1, dx=0, mods=""):
        return self.call("wheel", x=x, y=y, dy=dy, dx=dx, mods=mods)

    def key(self, key, mods=""):
        return self.call("key", key=key, mods=mods)

    def type(self, text):
        return self.call("type", text=text)

    def sleep(self, ms):
        return self.call("sleep", ms=ms)

    def screenshot(self, path):
        return self.call("screenshot", path=os.path.abspath(path))

    # ------------------------------------------------------------------ helpers
    def wait_for(self, pred, timeout=10.0, every=0.15):
        t0 = time.time()
        last = None
        while time.time() - t0 < timeout:
            last = pred()
            if last:
                return last
            time.sleep(every)
        raise UiTestError("wait_for timed out (last=%r)" % (last,))

    def widgets_with_text(self, text):
        return self.find(text=text)

    def dismiss_dialogs(self, button_texts=("Cancel", "Close", "OK", "No", "Later")):
        """Closes any modal window by clicking a harmless button (e.g. PixelLab's update prompt)."""
        for _ in range(4):
            wins = self.ui()["windows"]
            top = [w for w in wins if w["cls"].endswith("Window") and w["cls"] != "app::MainWindow"]
            if not top:
                return
            clicked = False
            for txt in button_texts:
                m = self.find(text=txt)
                if m:
                    self.click(m[-1]["rect"][0] + m[-1]["rect"][2] // 2,
                               m[-1]["rect"][1] + m[-1]["rect"][3] // 2)
                    clicked = True
                    break
            if not clicked:
                return
            time.sleep(0.3)

    def quit(self):
        try:
            self.call("kill")
        except UiTestError:
            pass
        try:
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.quit()
        if not self.keep:
            shutil.rmtree(self.workdir, ignore_errors=True)


def rect_center(r):
    return (r[0] + r[2] // 2, r[1] + r[3] // 2)


if __name__ == "__main__":
    print(__doc__)
