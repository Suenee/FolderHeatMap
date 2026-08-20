# Upgrade Protocol

This document defines the recommended pattern for `upgrade.cmd` in Windows repositories maintained in this project family. It combines the parts that worked well in FolderHeatMap, VoicePrompter Bridge, and the VoicePrompter Companion module, while explicitly avoiding failure modes observed during FolderHeatMap 1.12-1.15 bootstrap development.

## Goals

An upgrade must be deterministic, self-contained, safe for runtime data, and easy to diagnose remotely. A user should normally run only:

```bat
upgrade.cmd
```

The upgrader is responsible for bringing the local installation to the current selected branch, applying all required local changes, installing/updating dependencies, building the project, deploying it, and restoring the previous running state when appropriate.

The upgrader must either complete the whole operation or explain exactly why it did not.

## 1. Keep `upgrade.cmd` tiny

`upgrade.cmd` should be only a Windows launcher. Do not implement the full updater as a large batch script.

Recommended responsibilities of `upgrade.cmd`:

1. `cls` immediately so every interactive run starts on a clean console.
2. Resolve the repository directory from `%~dp0`.
3. Verify that Git is available and that the directory is a Git working tree.
4. Fetch the selected branch from `origin`.
5. Obtain the current PowerShell runner (`upgrade.ps1`) from that branch into a temporary file.
6. Pass the repository path to the runner through an environment variable.
7. Execute the temporary PowerShell runner.
8. Return exactly the runner exit code.

Do not let the running batch file replace itself and then continue executing. Do not use batch labels as the main upgrade architecture. Do not chain `CMD -> PowerShell -> temporary CMD -> CMD labels`.

The proven model is:

```text
upgrade.cmd
    -> fetch current upgrade.ps1
    -> temporary upgrade.ps1
    -> all upgrade logic
```

This avoids self-modifying batch files, `CALL :label` failures, CRLF/LF label parsing problems, and command-line quoting problems between interpreters.

## 2. The runner is authoritative

All substantial work belongs in `upgrade.ps1`:

- repository synchronization,
- local-change checks,
- dependency installation,
- process shutdown,
- migration/configuration work,
- build,
- package preparation,
- deploy,
- application restart,
- logging,
- status reporting.

Keep the runner versioned together with the application.

Recommended header:

```powershell
$Version = 'x.xx'
$Revision = 'x.xx-short-upgrade-revision'
```

The application version and the upgrade revision may differ. The application follows `x.xx`; the revision may add a short descriptive suffix for troubleshooting.

## 3. Self-update before the real upgrade

The updater must always prefer the updater stored on the target Git branch over the local copy.

The useful pattern from the VoicePrompter module is "update the updater first". The safer implementation learned from FolderHeatMap is not to overwrite the currently executing batch file. Instead:

- `upgrade.cmd` performs `git fetch origin`,
- reads the current `upgrade.ps1` from `origin/<branch>` into `%TEMP%`,
- executes that temporary runner,
- the runner then updates the repository itself.

This guarantees that even a very old local launcher can execute the newest upgrade logic, while avoiding the hazards of replacing a running `.cmd` file.

If the project uses a branch such as `devel`, it must be explicit and centralized. Never silently build another branch.

## 4. Repository synchronization rules

Before changing the working tree, verify the intended remote and branch.

Recommended flow:

```text
git fetch origin
verify/switch target branch
check tracked local changes
update branch from origin
verify HEAD == origin/<branch>
```

Local tracked changes must never be silently destroyed.

Two policies are acceptable, but choose one explicitly per repository:

- **strict policy**: abort and ask the user to commit/revert local tracked changes; this is the simplest and safest model used by VoicePrompter Bridge/module,
- **managed stash policy**: automatically stash tracked changes and clearly report that a stash was created; this is useful where upgrades are routinely run on a working development checkout.

Do not stash untracked runtime data by default.

Do not use `git reset --hard` unless the repository is deliberately treated as a disposable installation tree and the user-data boundaries are proven safe.

After update, verify repository state using Git semantics, not raw working-tree byte hashes. On Windows, CRLF working-tree files can differ byte-for-byte from Git blobs even when Git considers them clean.

Recommended checks:

```text
HEAD == origin/<branch>
git diff --quiet -- upgrade.cmd
git diff --quiet -- upgrade.ps1
HEAD:<file> blob == origin/<branch>:<file> blob
```

## 5. Line endings are part of the protocol

Windows scripts must have deterministic line endings.

Recommended `.gitattributes`:

```gitattributes
*.cmd text eol=crlf
*.bat text eol=crlf
*.ps1 text eol=crlf
```

This prevents batch-label and parser failures caused by scripts materialized with unexpected LF endings.

Avoid executing `.cmd` content reconstructed through PowerShell text pipelines. If a batch file ever must be materialized, preserve/normalize CRLF explicitly.

## 6. Runtime data must survive upgrades

The upgrade must distinguish source/build artifacts from runtime/user data.

Runtime files such as configuration, logs, databases, credentials, local state, and user-created content must be either:

- outside the source tree, or
- covered by `.gitignore`, or
- explicitly preserved by the upgrader.

A good pattern from VoicePrompter Bridge is to create a default runtime configuration only when it does not already exist:

```text
if config is missing -> copy example/default
if config exists -> preserve it
```

Do not overwrite an existing runtime configuration merely because a new version contains a newer example file. If a schema migration is required, perform an explicit migration step.

Avoid broad `git clean -fd` unless the repository is known to contain no valuable untracked files. Prefer removing known generated directories/files explicitly (`build`, `dist`, obsolete generated files, caches).

## 7. Stop only processes that actually exist

Before replacing files used by a running application, stop the relevant processes.

A useful pattern from VoicePrompter Bridge is to remember whether the application was running before the upgrade and restore that state afterwards.

Recommended sequence:

```text
record was_running
request normal shutdown
wait for graceful exit
if timeout -> warning + force stop
build/deploy
if was_running -> restart
```

Do not call `taskkill` blindly on several alternative executable names and treat "process not found" as an error. Detect the running process first.

If graceful shutdown is expected to flush caches/databases, give it a defined timeout. A forced stop after timeout should normally produce `WARNING`, not hide the fact that the upgrade otherwise succeeded.

## 8. Build and deploy are separate phases

The compiler/build system must write only into the build tree.

Do not use CMake/MSBuild `POST_BUILD` steps to copy binaries directly into a live `dist` or application directory. Live files may be locked by a previous process, Total Commander, antivirus, indexing software, or another consumer.

Recommended phases:

```text
configure
build all targets
verify all required artifacts exist
prepare dist/package
then deploy
```

Only after the complete build succeeds should the upgrader copy artifacts into `dist` and/or the live installation.

This makes failures atomic and keeps build errors separate from deployment/locking errors.

## 9. Dependencies belong to `upgrade.cmd`/runner

If the checked-out project requires local preparation, the upgrader must perform it automatically.

Examples:

- `npm install`
- `npm run build`
- downloading a required SDK/source archive
- installing/updating a local toolchain when the project policy allows it
- generating source/config files
- applying database/config migrations
- rebuilding native launchers

A fresh checkout plus `upgrade.cmd` should be enough to reach a runnable state, or the script must give a precise error explaining why it cannot.

Every external command must be checked by exit code.

## 10. Native command handling in PowerShell

On Windows PowerShell 5.1, native programs can write warnings to `stderr`, which PowerShell may expose as `ErrorRecord` objects. Do not interpret arbitrary `stderr` text as command failure.

For native executables, the process exit code is authoritative.

The runner should:

1. capture stdout/stderr for the log,
2. classify lines visually,
3. temporarily prevent harmless native stderr from becoming a terminating PowerShell exception,
4. read `$LASTEXITCODE`,
5. fail only when the exit code indicates failure (unless a command has a documented alternative success code).

This is especially important for Git, CMake/MSBuild, task utilities, and package managers.

## 11. Logging is mandatory for non-trivial upgraders

Create `upgrade.log` in the repository root.

Rules:

- single-run mode only,
- truncate at the beginning of every run,
- include all visible upgrade output,
- do not commit it (`*.log` or `upgrade.log` in `.gitignore`),
- make it sufficient for remote diagnosis without screenshots.

The header should contain at least:

```text
application/upgrade version
start date and time
repository path
branch
starting commit
runner architecture/revision
```

After self-update, log the exact build commit.

The final line must always be a status line.

Recommended final states:

```text
STATUS: SUCCESS - phase=COMPLETE
STATUS: WARNING - phase=COMPLETE
STATUS: FAILED - phase=<PHASE>
```

If an unexpected exception occurs, still write a final `STATUS: FAILED` line.

## 12. Console colors

Use colors only as status semantics:

- gray: normal progress / informational output,
- yellow: warning,
- red: error/failure,
- green: final successful completion.

The text must remain meaningful without color because `upgrade.log` is plain text.

Errors should not disappear in a long gray compiler log.

## 13. Upgrade phases

Use named phases so failures can be located immediately. A recommended generic phase set is:

```text
SELF-UPDATE
DEPENDENCIES
STOP-RUNTIME
CONFIGURATION / MIGRATION
CMAKE-CONFIGURE (if applicable)
BUILD
DIST / PACKAGE
DEPLOY
RESTART
COMPLETE
```

The visible progress may use `[1/N]`, `[2/N]`, etc., but the final failure status should use the stable phase name.

## 14. Idempotence

Running the upgrader twice in a row should be safe.

The second run should not:

- corrupt configuration,
- incrementally duplicate migrations,
- duplicate generated content,
- delete runtime data,
- require manual cleanup,
- fail merely because the previous run already completed.

Migration steps should test whether they are needed before applying them.

## 15. Failure behavior

On failure:

- stop immediately at the failed phase,
- do not deploy partially built artifacts,
- do not restart the application unless explicitly safe,
- preserve runtime/user data,
- write a red console error,
- write `STATUS: FAILED - phase=...` as the last log line,
- return non-zero exit code.

A failure message should say what failed and, when possible, what the user can do about it.

## 16. Success behavior

On success:

- verify required final artifacts exist,
- deploy them,
- restore the previous running state if the project uses that model,
- print key output paths,
- finish with `STATUS: SUCCESS` or `STATUS: WARNING`,
- return exit code `0`.

`WARNING` still means the requested upgrade completed, but something non-fatal occurred, for example a graceful-shutdown timeout that required a forced stop.

## 17. Things explicitly avoided

The following patterns caused or amplify real upgrade failures and should not be reintroduced without a strong reason:

- large monolithic `upgrade.cmd` files with many labels,
- self-overwriting a currently running `.cmd` and continuing execution,
- `CMD -> PowerShell -> CMD` bootstrap chains,
- passing repository paths through several quoted interpreter boundaries,
- relying on a quoted directory path with a trailing backslash,
- PowerShell mandatory bootstrap `param()` binding for a batch self-update chain,
- comparing raw CRLF working-tree bytes directly with LF Git blobs,
- treating every line on native `stderr` as a fatal error,
- killing executable alternatives that are not running and treating "not found" as failure,
- building directly into a live deployment directory,
- broad `git clean -fd` where untracked user/runtime files can exist,
- silently discarding local tracked changes.

## 18. Recommended project files

A non-trivial Windows project should normally contain:

```text
upgrade.cmd           tiny launcher
upgrade.ps1           authoritative upgrade runner
upgrade.log           generated, ignored, single-run
.gitattributes        Windows script line-ending rules
.gitignore            logs/runtime/build exclusions
CHANGELOG.md           application changes
UPGRADE.md             this protocol / project-specific upgrade notes
```

Projects may add migration/helper scripts, but the user-facing entry point remains `upgrade.cmd`.

## 19. Minimum acceptance test for an upgrader

Before an upgrader is considered stable, test at least these cases:

1. Current clean repository, application stopped.
2. Current clean repository, application running.
3. Local updater older than remote updater.
4. No source update available.
5. Remote source update available.
6. Build failure.
7. Deployment target temporarily locked.
8. Graceful process shutdown timeout.
9. Runtime config/log/database already exists and must survive.
10. Tracked local source modification.
11. Untracked runtime file present.
12. Repository path containing spaces.
13. Re-run immediately after a successful upgrade.
14. Verify the final `upgrade.log` line and process exit code for success, warning, and failure.

## 20. Design principle

The updater is part of the application, not an incidental helper script.

Prefer a small, boring, predictable launcher and one authoritative runner over clever self-modifying bootstrap logic. Every extra interpreter boundary, temporary representation, implicit encoding conversion, or live-file copy is another failure surface.

The desired user experience is simple:

```text
run upgrade.cmd
wait
read SUCCESS / WARNING / FAILED
if needed, send upgrade.log
```
