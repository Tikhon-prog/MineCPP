#!/usr/bin/env python3
"""Integration test for offline import and Java argument construction."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import uuid
import zipfile
import platform

exe = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
shutil.rmtree(root, ignore_errors=True)
version = "1.20.4"
minecraft = root / "minecraft"
version_dir = minecraft / "versions" / version
version_dir.mkdir(parents=True)
libraries_dir = minecraft / "libraries"
libraries_dir.mkdir()
(minecraft / "assets" / "indexes").mkdir(parents=True)
(minecraft / "assets" / "indexes" / "legacy.json").write_text("{}", encoding="utf-8")
system = platform.system().lower()
os_name = "windows" if system == "windows" else ("osx" if system == "darwin" else "linux")
classifier = "natives-windows-64" if os_name == "windows" else ("natives-osx" if os_name == "osx" else "natives-linux")
native_filename = "fake.dll" if os_name == "windows" else ("libfake.dylib" if os_name == "osx" else "libfake.so")
artifact_rel = "com/example/example-lib/1.0/example-lib-1.0.jar"
classifier_rel = f"org/example/native-lib/1.0/native-lib-1.0-{classifier}.jar"
artifact_path = libraries_dir / artifact_rel
artifact_path.parent.mkdir(parents=True)
with zipfile.ZipFile(artifact_path, "w") as libjar:
    libjar.writestr("example/Library.class", b"library")
native_path = libraries_dir / classifier_rel
native_path.parent.mkdir(parents=True)
with zipfile.ZipFile(native_path, "w", zipfile.ZIP_DEFLATED) as nativejar:
    nativejar.writestr(native_filename, b"native-binary-placeholder")
metadata = {
    "id": version,
    "type": "release",
    "mainClass": "net.minecraft.client.main.Main",
    "assetIndex": {"id": "legacy"},
    "libraries": [
        {"name": "com.example:example-lib:1.0", "downloads": {"artifact": {"path": artifact_rel}},
         "rules": [{"action": "allow", "os": {"name": os_name}}]},
        {"name": "org.example:native-lib:1.0", "natives": {os_name: "natives-windows-${arch}" if os_name == "windows" else classifier},
         "downloads": {"classifiers": {classifier: {"path": classifier_rel}}}}
    ],
    "arguments": {
        "jvm": ["-Xmx1G", "-Djava.library.path=${natives_directory}", "-cp", "${classpath}"],
        "game": ["--username", "${auth_player_name}", "--version", "${version_name}",
                 "--gameDir", "${game_directory}", "--assetsDir", "${assets_root}",
                 "--assetIndex", "${assets_index_name}", "--uuid", "${auth_uuid}",
                 "--accessToken", "${auth_access_token}", "--userType", "${user_type}",
                 "--clientId", "${clientid}", "--xuid", "${auth_xuid}"]
    }
}
(version_dir / f"{version}.json").write_text(json.dumps(metadata), encoding="utf-8")
with zipfile.ZipFile(version_dir / f"{version}.jar", "w", zipfile.ZIP_DEFLATED) as jar:
    jar.writestr("net/minecraft/client/main/Main.class", b"synthetic-class")

data = root / "minecpp-data"
subprocess.run([str(exe), "import", version, "--minecraft-dir", str(minecraft), "--data-dir", str(data)], check=True, capture_output=True, text=True)
profile = data / "profiles" / version
assert (profile / "client.jar").is_file()
assert (profile / "extracted" / "net/minecraft/client/main/Main.class").read_bytes() == b"synthetic-class"

run = subprocess.run([str(exe), "launch", version, "--username", "Tikhon", "--minecraft-dir", str(minecraft),
                      "--data-dir", str(data), "--dry-run"], check=True, capture_output=True, text=True)
assert "net.minecraft.client.main.Main" in run.stdout, run.stdout
assert str(profile / "extracted") in run.stdout, run.stdout
assert "--username Tikhon" in run.stdout, run.stdout
assert "--version 1.20.4" in run.stdout, run.stdout
assert "--accessToken 0" in run.stdout, run.stdout
assert '--clientId ""' in run.stdout, run.stdout
assert "example-lib-1.0.jar" in run.stdout, run.stdout
assert (profile / "natives" / native_filename).read_bytes() == b"native-binary-placeholder"
expected = uuid.UUID(bytes=hashlib.md5(b"OfflinePlayer:Tikhon").digest(), version=3)
assert str(expected) in run.stdout, (str(expected), run.stdout)
assert "https://" not in run.stdout and "http://" not in run.stdout
# Import also accepts an existing JAR/metadata pair outside the standard versions/<id> folder.
external_jar = root / "external-client.jar"
shutil.copy2(version_dir / f"{version}.jar", external_jar)
external_data = root / "external-data"
subprocess.run([str(exe), "import", version, "--jar", str(external_jar), "--metadata", str(version_dir / f"{version}.json"),
                "--minecraft-dir", str(minecraft), "--data-dir", str(external_data)], check=True, capture_output=True, text=True)
assert (external_data / "profiles" / version / "extracted" / "net/minecraft/client/main/Main.class").is_file()

# A failed re-import must not delete the previously imported working profile.
(version_dir / f"{version}.jar").write_bytes(b"not a zip file")
failed = subprocess.run([str(exe), "import", version, "--minecraft-dir", str(minecraft), "--data-dir", str(data)],
                        capture_output=True, text=True)
assert failed.returncode != 0
assert (profile / "client.jar").is_file()
assert (profile / "extracted" / "net/minecraft/client/main/Main.class").is_file()

print("MineCPP offline-launch smoke test passed")
