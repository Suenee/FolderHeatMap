# Upgrade Protocol

This document defines the recommended pattern for `upgrade.cmd` in Windows repositories maintained in this project family. It combines the parts that worked well in FolderHeatMap, VoicePrompter Bridge, and the VoicePrompter Companion module, while explicitly avoiding failure modes observed during FolderHeatMap 1.12-1.15 bootstrap development.

## Goals

An upgrade must be deterministic, self-contained, safe for runtime data, and easy to diagnose remotely. A user should normally run only `upgrade.cmd`.

The upgrader is responsible for bringing the local installation to the current selected branch, applying all required local changes, installing/updating dependencies, building the project, deploying it, and restoring the previous running state when appropriate. It must either complete the whole operation or explain exactly why it did not.

## 1. Keep `upgrade.cmd` tiny

`upgrade.cmd` should be only a Windows launcher. Do not implement the full updater as a large batch script.

Recommended responsibilities: clear the console for an interactive run; resolve `%~dp0`; verify Git and the working tree; fetch the selected branch; obtain the current `upgrade.ps1` into a temporary file; pass the repository path through an environment variable; execute the runner; return exactly its exit code.

Do not let a running batch file replace itself and then continue executing. Do not use batch labels as the main architecture. The proven model is `upgrade.cmd -> current temporary upgrade.ps1 -> all upgrade logic`.

## 2. The runner is authoritative

All substantial work belongs in `upgrade.ps1`: repository synchronization, local-change checks, dependency installation, process shutdown, migration/configuration, build, package preparation, deploy, application restart, logging, and status reporting.

Keep both an application version (`x.xx`) and a diagnostic runner revision (`x.xx-short-revision`).

## 3. Self-update before the real upgrade

Always prefer the updater stored on the target Git branch over the local runner. Fetch the current `upgrade.ps1` and execute a temporary copy. Do not overwrite the currently executing batch file. The target branch must be explicit and centralized.

## 4. Repository synchronization rules

Verify remote and branch, fetch, check local tracked changes, update the intended branch, then verify `HEAD == origin/<branch>`.

Local tracked changes must never be silently destroyed. Use either a strict abort policy or an explicitly reported managed stash policy. Do not stash untracked runtime data by default. Do not use `git reset --hard` unless the checkout is deliberately disposable and user-data boundaries are proven safe. A controlled upgrader may use it only after all non-bootstrap tracked edits have been preserved and when runtime/user data is known to be untracked or external; log that synchronization explicitly.

Use Git semantics for verification, not raw working-tree byte hashes. CRLF working-tree files can differ byte-for-byte from Git blobs while still being clean.

## 5. Line endings are part of the protocol

Recommended `.gitattributes`:

```gitattributes
*.cmd text eol=crlf
*.bat text eol=crlf
*.ps1 text eol=crlf
```

Avoid executing `.cmd` content reconstructed through PowerShell text pipelines. If a batch file must be materialized, preserve or normalize CRLF explicitly.

## 6. Runtime data must survive upgrades

Configuration, logs, databases, credentials, local state, and user-created content must be outside the source tree, ignored, or explicitly preserved. Create default configuration only when it does not already exist. Schema changes require explicit migrations.

Avoid broad `git clean -fd` where valuable untracked files may exist. Prefer removing known generated directories and files.

## 7. Stop only processes that actually exist

Record whether the application was running, request graceful shutdown, wait for a defined timeout, warn and force-stop only if necessary, then restore the previous running state after successful deployment.

Do not blindly `taskkill` alternative executable names and treat `not found` as failure.

## 8. Build and deploy are separate phases

Build systems write only into the build tree. Do not copy binaries into a live `dist` or installation directory from CMake/MSBuild `POST_BUILD` actions.

Use: configure -> build all targets -> verify artifacts -> prepare dist/package -> deploy.

## 9. Dependencies belong to the upgrader

A fresh checkout plus `upgrade.cmd` should reach a runnable state or give a precise reason why it cannot. Handle required package installs, downloads, generated files, migrations, and native launcher builds automatically. Check every external command by exit code.

## 10. Native command handling in PowerShell

On Windows PowerShell 5.1, native programs may write harmless warnings to stderr as PowerShell `ErrorRecord` objects. For native executables, the process exit code is authoritative. Capture output, classify it visually, prevent harmless stderr from becoming a terminating PowerShell exception, then inspect `$LASTEXITCODE`.

## 11. Logging is mandatory

Create repository-root `upgrade.log` in single-run mode. Truncate it at each run, include all visible output, ignore it in Git, and make it sufficient for remote diagnosis without screenshots.

Header: upgrade revision, date/time, repository, branch, starting commit, runner architecture. After self-update, log the exact build commit.

The final line must always be one of:

```text
STATUS: SUCCESS - phase=COMPLETE
STATUS: WARNING - phase=COMPLETE
STATUS: FAILED - phase=<PHASE>
```

## 12. Console colors

Gray = normal information, yellow = warning, red = failure, green = successful completion. Text must remain meaningful without color because the log is plain text.

## 13. Upgrade phases

Use stable phase names such as `SELF-UPDATE`, `DEPENDENCIES`, `STOP-RUNTIME`, `CONFIGURATION`, `MIGRATION`, `CMAKE-CONFIGURE`, `BUILD`, `DIST`, `DEPLOY`, `RESTART`, and `COMPLETE`.

## 14. Idempotence

Two consecutive runs must be safe. The second run must not corrupt configuration, duplicate migrations/content, delete runtime data, require manual cleanup, or fail merely because the first run completed.

## 15. Failure behavior

Stop at the failed phase; do not deploy partial artifacts; preserve runtime data; log a clear red error; end with `STATUS: FAILED`; return non-zero. Do not restart the application unless that is explicitly safe.

## 16. Success behavior

Verify final artifacts, deploy, restore previous running state where applicable, print important paths, end with `STATUS: SUCCESS` or `STATUS: WARNING`, and return zero. `WARNING` means the requested upgrade completed but a non-fatal condition occurred.

## 17. Upgrade buglist: known traps and failure signatures

This section is a mandatory pre-flight checklist when creating or modifying an upgrader. These are not theoretical warnings; most were observed in real project upgrades.

### Bootstrap and interpreter boundary bugs

- **Self-overwriting a running `.cmd`:** after Git replaces the file on disk, `cmd.exe` may continue reading different/new content and execute an impossible mixture of old and new updater logic. Symptom: messages from two updater generations appear in one run. Prevention: keep the launcher tiny and run the real logic from a temporary `upgrade.ps1`.
- **`CMD -> PowerShell -> CMD` chains:** quoting, escaping, trailing backslashes, environment expansion, and exit codes become fragile at every boundary. Prevention: one CMD launcher, one PowerShell runner.
- **Batch labels in downloaded/generated scripts:** LF/CRLF changes can cause `The system cannot find the batch label specified`. Prevention: do not make label-heavy batch files authoritative; enforce CRLF for Windows scripts.
- **PowerShell `param()` bootstrap collisions:** parameter names can collide with PowerShell automatic/common semantics, and mandatory parameters can trigger interactive `Supply values...` prompts. Prevention: do not use complex PowerShell parameter binding as a self-update transport; use controlled environment variables for bootstrap state.
- **Trailing backslash in a quoted path:** a repository path ending in `\` can damage quoting when transported through multiple interpreters. Prevention: normalize repository paths and trim the trailing separator before transport.
- **Native argument-array binding:** a wrapper can accidentally invoke `git.exe` without `fetch origin` if its argument-list parameter is ambiguously bound. Symptom: Git prints its generic usage page. Prevention: explicit named PowerShell parameters and a dedicated native-command helper; test the exact resulting invocation.

### Git and self-update bugs

- **Raw hash comparison of working files against Git blobs:** CRLF vs LF produces a false mismatch even though Git considers the file clean. Prevention: use Git diff/blob semantics and `HEAD == origin/<branch>`.
- **Updater changes itself during `stash`, `pull`, `checkout`, or `reset`:** never assume the source file currently executing is immutable. The launcher must not depend on rereading itself after repository mutation.
- **Managed stash reports success but bootstrap remains dirty:** on Windows, line-ending materialization can leave `upgrade.cmd` looking locally modified even after a successful tracked-file stash, causing the following pull/merge to abort with `local changes ... would be overwritten`. Prevention: treat bootstrap files as authoritative remote state rather than user data; preserve real tracked edits separately, then synchronize the tracked installation tree deterministically to the fetched target commit while leaving untracked runtime data untouched.
- **Stashing untracked runtime files:** `git stash -u` can unexpectedly capture local configuration/log/state. Prevention: stash tracked files only unless untracked handling is explicitly designed.
- **`git clean -fd` deleting useful local data:** never use it broadly unless the repository is a proven disposable installation tree. Prefer explicit generated-path cleanup.
- **Wrong branch silently built:** branch selection must be explicit; verify the final `HEAD` against `origin/<target>` before build.
- **Remote URL drift:** verify/set the expected `origin` where repository policy requires it.
- **Old launcher cannot reach new runner:** the self-update mechanism itself must remain backward-compatible enough for an old launcher to fetch the current authoritative runner. Keep this path extremely simple.

### PowerShell and native-process bugs

- **stderr is not the same as failure:** Git and build tools legitimately emit warnings/progress to stderr. Symptom: a warning such as CRLF conversion aborts the upgrade. Prevention: exit code decides native command success.
- **`$ErrorActionPreference = 'Stop'` plus native stderr:** Windows PowerShell 5.1 may turn native stderr into terminating behavior through the pipeline. Native-command wrappers must isolate this behavior.
- **`$LASTEXITCODE` read too late:** another native command can overwrite it. Capture it immediately after the process being tested.
- **Missing process treated as fatal:** attempting to kill both x86/x64 executable names can fail on the one that does not exist. Detect first; stop only running processes.
- **Output encoding/garbled compiler text:** console code pages and PowerShell encoding can corrupt localized MSBuild output. This should not change success/failure semantics; log raw-enough diagnostic information and prefer stable English/status identifiers where possible.

### Runtime shutdown and file-lock bugs

- **Assuming a process stopped immediately:** wait for a defined graceful-shutdown interval before deployment.
- **Graceful shutdown timeout hides data-flush risk:** if the application persists cache/database state on exit, timeout + forced kill must be a visible warning.
- **Building into live `dist`:** `POST_BUILD` copy can fail because the old executable/plugin is still locked by the host, antivirus, indexer, or scanner. Build only to the build tree; deploy later.
- **Using live `dist` as the DIST package:** if Total Commander is registered directly against `dist\FolderHeatMap.wdx64`, preparing `dist` is already a live deployment and can fail before the DEPLOY phase with a sharing violation. Symptom: build succeeds, then DIST fails with `file is being used by another process`. Prevention: stage packages in an isolated build-tree directory, verify them there, and only then copy into the live destination during DEPLOY with bounded retry and explicit lock diagnostics.
- **Host process still owns plugin DLL/WDX:** stopping only the worker may not release the plugin. Identify all file owners/host processes required by the project.
- **Partial deployment:** never begin replacing live artifacts before all required build outputs have been verified.

### Configuration and data-loss bugs

- **Runtime configuration tracked accidentally:** `%APPDATA%`, logs, DB files, or generated local configuration must not enter Git merely because a script used a wrong relative path.
- **Environment variable text used as a literal path:** strings such as `%APPDATA%/...` can accidentally become repository-relative filenames if expansion occurs in the wrong interpreter. Resolve runtime paths in one authoritative layer and validate that they are absolute where required.
- **Default config overwrites user config:** copy defaults only when missing; migrate existing config explicitly.
- **Database/schema migration is not idempotent:** every migration must detect whether it has already run.
- **Upgrade log stored in user profile:** project upgrade diagnostics belong in the repository root unless a project explicitly specifies otherwise. Keep `upgrade.log` ignored and single-run.

### Build and dependency bugs

- **Build succeeds but required artifact is missing:** verify every required output before DIST/DEPLOY.
- **Dependency download succeeds partially:** validate downloaded/extracted required files, not only the downloader exit code.
- **Tool found in one shell but not another:** resolve the actual executable path (Git, CMake, npm, compiler) before relying on nested shell PATH behavior.
- **Generated source/config is stale:** regenerate deterministically or explicitly verify freshness before build.
- **Build directory contains stale state:** when correctness matters more than incremental speed, recreate the build directory or provide a reliable invalidation strategy.

### Logging/status bugs

- **Failure without final status:** catch unexpected exceptions and always append `STATUS: FAILED - phase=...`.
- **Log says failure but process exits zero (or inverse):** final status and process exit code must agree.
- **Warning shown as red/fatal:** classification is presentation; only defined semantics/exit codes determine failure.
- **New run appends to an old log:** `upgrade.log` is single-run and must be truncated before diagnostic output begins.
- **Console starts in old output:** interactive launcher should `cls` once at the start; internal/recovery execution should not repeatedly clear diagnostics.

### Buglist rule

Whenever a new updater defect is discovered, do not only patch the code. Add the failure mode, recognizable symptom, root cause, and prevention rule to this buglist. The purpose is to make the same class of bug occur only once across the entire project family.

## 18. Patterns explicitly avoided

Do not reintroduce without a strong, documented reason: large monolithic batch files with many labels; self-overwriting running batch files; multi-interpreter bootstrap chains; raw CRLF/blob comparisons; arbitrary stderr-as-failure; blind process killing; build-to-live-directory; broad untracked-file cleanup; silent destruction of local tracked changes.

## 19. Recommended project files

```text
upgrade.cmd           tiny launcher
upgrade.ps1           authoritative upgrade runner
upgrade.log           generated, ignored, single-run
.gitattributes        Windows script line-ending rules
.gitignore            logs/runtime/build exclusions
CHANGELOG.md           application changes
UPGRADE.md             protocol + buglist + project-specific notes
```

## 20. Minimum acceptance test

Before declaring an upgrader stable, test: clean repo/app stopped; clean repo/app running; old local updater; no update; remote update; build failure; locked deployment target; graceful shutdown timeout; existing runtime config/log/database; tracked local modification; untracked runtime file; repository path with spaces; immediate second run; final log status and exit code for success/warning/failure.

Also deliberately test at least one harmless native stderr warning and verify that it remains a warning rather than becoming a failed upgrade.

## 21. Design principle

The updater is part of the application, not an incidental helper. Prefer a small, boring, predictable launcher and one authoritative runner over clever self-modifying bootstrap logic. Every interpreter boundary, implicit encoding conversion, or live-file copy is another failure surface.

Desired user experience: run `upgrade.cmd`, wait, read `SUCCESS / WARNING / FAILED`, and if necessary send `upgrade.log`.
