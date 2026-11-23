#!/usr/bin/env python3
"""Small integration test: make a local ZIP/JAR and exercise inspect/extract."""
import pathlib
import subprocess
import sys
import zipfile

exe = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
archive = out.parent / "minecpp-smoke.jar"
out.mkdir(parents=True, exist_ok=True)
if out.exists():
    for item in out.iterdir():
        if item.is_file():
            item.unlink()
        elif item.is_dir():
            import shutil
            shutil.rmtree(item)
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as jar:
    jar.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n")
    jar.writestr("net/minecraft/client/main/Main.class", b"test-class-data")
    jar.writestr("assets/minecraft/test.txt", "offline test\n")

inspect = subprocess.run([str(exe), "inspect", str(archive)], check=True, capture_output=True, text=True)
assert "Java classes: 1" in inspect.stdout, inspect.stdout
subprocess.run([str(exe), "extract", str(archive), "--output", str(out)], check=True)
assert (out / "META-INF" / "MANIFEST.MF").read_text() == "Manifest-Version: 1.0\n"
assert (out / "assets" / "minecraft" / "test.txt").read_text() == "offline test\n"
assert (out / "net" / "minecraft" / "client" / "main" / "Main.class").read_bytes() == b"test-class-data"
# ZIP-slip paths must be refused.
unsafe = out.parent / "minecpp-unsafe.jar"
with zipfile.ZipFile(unsafe, "w", compression=zipfile.ZIP_DEFLATED) as jar:
    jar.writestr("../escaped.txt", "not allowed")
result = subprocess.run([str(exe), "extract", str(unsafe), "--output", str(out / "unsafe")], capture_output=True, text=True)
assert result.returncode != 0, "unsafe ZIP entry unexpectedly extracted"
assert not (out.parent / "escaped.txt").exists()
print("MineCPP ZIP smoke test passed")
