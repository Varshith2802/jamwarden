# PlatformIO pre-build script: copy the portable detector (../../core) into lib/jamwarden so that the
# firmware always builds the same code that the PC tests and the evaluation use.
import shutil
from pathlib import Path

try:
    Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
    project_dir = Path(env["PROJECT_DIR"])  # noqa: F821
except NameError:  # run directly: python sync_core.py
    project_dir = Path(__file__).resolve().parent

core = (project_dir / ".." / ".." / "core").resolve()
dst = project_dir / "lib" / "jamwarden" / "src"
(dst / "jamwarden").mkdir(parents=True, exist_ok=True)
for h in (core / "include" / "jamwarden").glob("*.hpp"):
    shutil.copy2(h, dst / "jamwarden" / h.name)
for c in (core / "src").glob("*.cpp"):
    shutil.copy2(c, dst / c.name)
print(f"[jamwarden] synced core from {core}")
