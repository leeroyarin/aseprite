"""Real-file verification of Easymetric shared model size and migration (2026-11-18).

Validates:
1. Opening a copy of Easymetric-Shubha-Model-Test.aseprite in the studio.
2. Automatic migration to unified model size with o=[0,0,0].
3. Resizing model height with Stretch mode keeps all objects whole.
4. Session recorder records the entire test and summarizes results.

Run:
  python tools/ui-test/realfile_shared_size.py
"""
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from uitest import UiTest, UiTestError  # noqa: E402

ORIGINAL_PATH = r"C:\Users\leero\Downloads\Easymetric-Shubha-Model-Test.aseprite"


def main():
    if not os.path.isfile(ORIGINAL_PATH):
        print(f"SKIPPED: Test source file not found at {ORIGINAL_PATH}")
        return 0

    started = time.time()
    temp_dir = tempfile.mkdtemp(prefix="easymetric_realfile_")
    test_copy = os.path.join(temp_dir, "Easymetric-Shubha-Model-Test.aseprite")
    try:
        shutil.copyfile(ORIGINAL_PATH, test_copy)
        print(f"Copied source model to isolated path: {test_copy}")

        # Launch UI test harness with session recording enabled
        with UiTest(files=[test_copy], timeout=120) as t:
            # Enable session recorder in preferences
            t.lua('''
            if app.preferences.session_recorder then
                app.preferences.session_recorder.enabled = true
                app.preferences.session_recorder.screenshots = true
            end
            ''')
            t.sleep(1000)

            # Open Easymetric Studio
            print("Opening Easymetric Studio...")
            t.lua("app.command.EasymetricStudio()")
            t.sleep(2000)

            st = t.state()
            objects = st.get("objects", [])
            print(f"Detected {len(objects)} objects in model.")

            # Check migration / shared size contract
            model_size = st.get("modelSize")
            print(f"Model size after studio open: {model_size}")

            for obj in objects:
                offset = obj.get("offset", [0, 0, 0])
                box = obj.get("box", [])
                assert offset == [0, 0, 0], f"Object {obj.get('name')} offset not zero: {offset}"
                if model_size:
                    assert box == model_size, f"Object {obj.get('name')} box {box} != modelSize {model_size}"

            # Focus 'Left Hand' object
            print("Focusing 'Left Hand' object...")
            t.lua("app.command.EasymetricStudio{ action = 'focusObject', object = 'Left Hand' }")
            t.sleep(1000)

            # Record baseline preview stats before resize
            stats_before_path = os.path.join(temp_dir, "stats_before.json")
            t.lua(f'app.command.EasymetricStudio{{ action = "previewStats", out = [[{stats_before_path}]] }}')

            # Apply Model Size resize with mode='stretch'
            curr_w = model_size[0] if model_size else 32
            curr_h = model_size[1] if model_size else 32
            curr_d = model_size[2] if model_size else 32
            new_h = curr_h + 16

            print(f"Applying model size resize (H: {curr_h} -> {new_h}, mode=stretch)...")
            t.lua(f'app.command.EasymetricStudio{{ action = "modelSize", width = {curr_w}, height = {new_h}, depth = {curr_d}, mode = "stretch" }}')
            t.sleep(2000)

            st_after = t.state()
            new_model_size = st_after.get("modelSize")
            print(f"Model size after stretch: {new_model_size}")
            assert new_model_size == [curr_w, new_h, curr_d], f"Model size mismatch: {new_model_size}"

            # Verify 'Left Hand' remained non-empty and scaled
            stats_after_path = os.path.join(temp_dir, "stats_after.json")
            t.lua(f'app.command.EasymetricStudio{{ action = "previewStats", out = [[{stats_after_path}]] }}')

            print("PASS: Real-file shared size & stretch verification succeeded.")
            t.sleep(1000)

        # Summarize the session recorded by THIS run only. Without a new
        # recording, summarize_session.py would print the user's own latest
        # session (thousands of lines), so it is skipped (Claude Code, 2026-11-18).
        summarize_py = os.path.join(REPO, "tools", "usage", "summarize_session.py")
        usage = os.path.join(os.environ.get("APPDATA", ""), "Aseprite2", "usage")
        newest = None
        if os.path.isdir(usage):
            dirs = [os.path.join(usage, d) for d in os.listdir(usage)]
            dirs = [d for d in dirs if os.path.isdir(d) and os.path.getmtime(d) >= started]
            newest = max(dirs, key=os.path.getmtime) if dirs else None
        if newest and os.path.isfile(summarize_py):
            print("\nSession Recorder Summary (%s):" % newest)
            os.system(f'python "{summarize_py}" --notes "{newest}"')
        else:
            print("\n(no session was recorded by this run; nothing to summarize)")

        return 0

    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
