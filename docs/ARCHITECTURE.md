# Architecture — MineCPP 0.1.0

## Data flow

```text
Existing Minecraft installation
  ├── versions/<id>/<id>.json
  ├── versions/<id>/<id>.jar
  ├── libraries/
  └── assets/
          |
          v
MineCPP import
  ├── validates the JAR central directory and CRCs
  ├── copies the version JSON and original client JAR
  └── extracts JAR entries into profiles/<id>/extracted/
          |
          v
MineCPP launch (offline)
  ├── reads version metadata
  ├── resolves already-present libraries and native classifiers
  ├── builds a classpath with the extracted client tree first, original JAR as fallback, and local libraries
  ├── supplies a deterministic OfflinePlayer identity
  └── starts the local Java runtime; no network code is involved
```

## Module boundaries

- `main.cpp`: CLI commands and option parsing.
- `json.*`: small read-only JSON parser for Mojang version metadata.
- `zip.*`: ZIP/JAR central-directory reader, deflate decompression, CRC validation and safe extraction.
- `minecraft.*`: default paths, version discovery/import, launch metadata and argument construction.
- `process.*`: Java runtime lookup and OS process creation.

## Import layout

```text
<MineCPP data>/profiles/<version>/
  version.json
  client.jar
  source-game-dir.txt
  extracted/       # expanded copy of client.jar
  natives/         # recreated before launching
```

The original `.minecraft` directory is read-only from MineCPP's point of view. Libraries and assets remain in that installation to avoid duplicating large data trees. The source directory can be overridden at launch time. Import also accepts an explicit `--jar` and `--metadata` pair when the JAR is stored outside the normal `versions/<id>/` folder.

## Safety and reliability

- ZIP paths are normalized and absolute paths or `..` traversal are rejected.
- Each JAR entry is size-limited; total extraction is capped at 4 GiB.
- CRC32 is checked after decompression.
- Import is built in a staging directory and renamed into place after successful extraction.
- No game binaries, JARs, assets, libraries, worlds, accounts or prepared data belong in Git.

## Next milestones

1. Add integration fixtures from representative vanilla version JSONs (legacy and modern).
2. Improve launch compatibility (OS-version rules, Java major-version checks, asset object validation and logging paths).
3. Add profiles/instances with per-profile game directories and memory settings.
4. Add a desktop UI after the import/launch core is stable.
5. Decide separately whether MineCPP becomes a Java launcher only or starts a native C++ rendering/gameplay engine. A native engine requires far more than extracting the client JAR.
