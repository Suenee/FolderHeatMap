# Upgrade Protocol

This document is the master upgrade standard for Windows repositories in this project family. It consolidates proven practices from FolderHeatMap, VoicePrompter, VoicePrompterBridge / Socket Universe Bridge, VirtualMonitorsUniverse, Companion modules, and related Wipe Codes projects.

The purpose is not merely to describe one updater. It is a shared engineering memory: once an upgrade failure has been understood and solved, the same class of failure should not be rediscovered in another repository.

## 1. Core contract

A user should normally need only one command:

```text
upgrade.cmd
```

A correct upgrader must be deterministic, self-contained, safe for user/runtime data, compatible with repositories stored on local or network drives, diagnosable from one log, and idempotent.

A fresh checkout plus `upgrade.cmd` should reach a runnable state, or stop with a precise explanation of what prevented it.

The updater owns the complete lifecycle required by the project: bootstrap, repository synchronization, dependencies, shutdown of project-owned runtime processes, configuration/migrations, build/test/package/deploy, verification, and restoration of the previous running state where appropriate.

## 2. Architecture: tiny launcher, authoritative runner

Prefer this architecture:

```text
upgrade.cmd -> current temporary upgrade.ps1 -> upgrade lifecycle
```

`upgrade.cmd` is a bootstrap launcher, not the main application. Keep it as small and stable as practical. Substantial logic belongs in `upgrade.ps1`.

The launcher may resolve the repository path, make UNC paths usable, bootstrap Git when project policy allows it, fetch the target branch, extract the current runner to `%TEMP%`, establish narrowly scoped environment state, invoke PowerShell, remove the temporary runner, and return exactly its exit code.

Do not build a large label-heavy batch updater unless a project has a compelling documented reason. Do not create repeated `CMD -> PowerShell -> CMD -> PowerShell` interpreter chains.

Keep an application version in `x.xx` form and, where useful for diagnostics, a separate updater revision.

## 3. Self-update is phase zero

The updater in the target branch is authoritative. An old local updater must be able to reach and execute the current upgrade implementation before performing the real upgrade.

Never overwrite a running script and then rely on that process continuing to read the same file. The safest general pattern is to fetch the target branch, extract `upgrade.ps1` to a unique temporary path, and execute that copy.

If the project still uses a batch implementation as its authoritative updater, extract the remote `upgrade.cmd` to `%TEMP%`, explicitly normalize it to CRLF, and execute the temporary copy synchronously. This is proven to avoid both self-overwrite and LF-only batch-label failures.

The self-update path must remain deliberately simple and backward-compatible. It is the recovery path for old installations.

## 4. Repository paths: local, mapped and UNC are all valid

Repository data may live on a network drive. This is a supported configuration, not an exceptional error case.

Never hard-code a drive letter, checkout location, username, or workstation-specific path. Resolve the repository from `%~dp0` or an equivalent canonical source.

For batch launchers prefer:

```bat
pushd "%REPO_DIR%"
```

over `cd /d`. `pushd` works with ordinary paths and maps UNC paths to a temporary drive letter for tools that cannot operate directly on UNC paths. Always pair it with `popd`.

Normalize paths at interpreter boundaries. In particular, trim an unnecessary trailing backslash before transporting a quoted path through CMD/PowerShell arguments or environment variables.

Do not assume that a dependency manager behaves well when its cache is on a network share. Decide cache placement explicitly. Project-owned persistent caches may intentionally stay under a repository `.cache` directory when portability is required; tools known to misbehave on network storage may use a local machine cache. Temporary bootstrap runners belong in `%TEMP%`.

Document the chosen policy per project rather than allowing a package manager to choose accidental locations.

## 5. Git `safe.directory` and network repositories

Git may report `fatal: detected dubious ownership in repository` for legitimate mapped/NAS/UNC repositories.

This error must never be interpreted as proof that `.git` is missing. Otherwise an upgrader can incorrectly start a second bootstrap/clone inside an existing repository.

Prefer a process-scoped exception instead of permanently weakening global Git security. A proven pattern is to set `GIT_CONFIG_COUNT`, `GIT_CONFIG_KEY_0=safe.directory`, and `GIT_CONFIG_VALUE_0` to the exact selected repository path for the updater and its children.

Do not use global `safe.directory=*`. A wildcard scoped only to a short-lived updater process is safer than a global wildcard, but an exact repository path is the preferred final design.

If an older bootstrap must repair global configuration, detect the exact `dubious ownership` diagnostic and register only Git's suggested repository path.

## 6. Fresh-machine bootstrap

Where project policy supports installation through `upgrade.cmd`, a copied launcher should be able to bootstrap a new Windows machine.

The bootstrap may:

1. verify PowerShell;
2. locate Git;
3. install Git through the documented Windows package manager when allowed;
4. refresh the current process PATH after installation;
5. initialize or clone the intended repository;
6. fetch the explicit target branch;
7. hand control to the current remote updater.

Never initialize over an arbitrary populated directory. A fresh-bootstrap directory must be empty except for explicitly allowed bootstrap files such as `upgrade.cmd` and a log directory.

If installation is intentionally separate, `upgrade.cmd` must say so clearly and direct a fresh machine to `install.cmd` rather than attempting a partial bootstrap.

When an installer modifies PATH, remember that the already-running `cmd.exe` does not inherit the new environment. Probe known executable locations or update the current process PATH before concluding that installation failed.

## 7. Repository synchronization

The target remote and branch must be explicit and centralized.

Recommended sequence:

1. verify repository identity;
2. verify/set the expected `origin` according to project policy;
3. fetch the target branch;
4. inspect tracked/staged local changes;
5. preserve or reject real local edits according to policy;
6. synchronize the intended branch;
7. verify the active branch;
8. verify `HEAD == origin/<target>` before building.

Never silently destroy real user/developer tracked changes.

Two acceptable policies are:

- strict: abort and explain that tracked changes must be committed/reverted;
- managed: preserve explicitly identified tracked changes and report exactly what happened.

Do not stash untracked runtime data by default. `git stash -u` is dangerous in application repositories.

A deterministic `reset --hard origin/<branch>` is acceptable only when the repository's tracked installation tree is explicitly disposable and real local edits have already been rejected or preserved. Runtime/user data must be proven external, ignored, or otherwise protected.

Do not use broad `git clean -fd`. Delete only known generated paths.

## 8. Line endings are part of the upgrade protocol

Windows scripts should have explicit repository rules, normally:

```gitattributes
*.cmd text eol=crlf
*.bat text eol=crlf
*.ps1 text eol=crlf
```

Git stores text canonically, so content obtained through `git show` can be LF-only even when the working-tree policy is CRLF. `cmd.exe` label processing has proven unreliable with generated/downloaded LF-only batch files.

If a `.cmd` is materialized from Git and executed, normalize the temporary executable copy explicitly to CRLF.

Never use raw byte/hash equality between a CRLF working-tree file and a Git blob as a cleanliness test. Use Git semantics (`git diff`, index state, commit/tree identity). If a synchronized updater file must be verified, use a comparison whose normalization semantics are understood and tested.

Do not repeatedly “fix” line endings by rewriting tracked scripts during every upgrade. That creates the dirty-tree/stash loop we have already encountered.

## 9. Runtime and user data are sacred

Configuration, logs, databases, credentials, user content, runtime state, caches with user value, and machine-specific settings must be outside the tracked source tree, ignored, or explicitly preserved.

Default configuration is created only when missing. Existing configuration is migrated, not replaced.

Every migration must be idempotent and detect whether it has already been applied.

Validate environment-expanded paths before writing. A literal string such as `%APPDATA%/...` must never accidentally become a repository-relative filename because expansion happened in the wrong interpreter.

Repository cleanup must be allow-list based: remove known generated artifacts, obsolete project-owned directories, `bin/obj`, build output, or documented transient files. Do not infer that every untracked file is disposable.

## 10. Process ownership: stop only what belongs to the project

Before modifying files that may be locked, determine whether the project was running.

Do not kill processes merely by generic executable name when that could affect unrelated applications. For shared runtimes such as `node.exe`, `dotnet.exe`, `python.exe`, or `esbuild.exe`, identify project ownership from executable path, command line, working/repository path, parent/child relationship, or another project-specific marker.

When stopping a process tree, capture the owned process set first, include owned descendants, and leave unrelated processes untouched.

Prefer graceful shutdown, wait a bounded interval, then force-stop only when necessary and report that as a warning when data flushing may be affected.

A process disappearing between enumeration and termination is not an error; it may have exited normally.

## 11. Preserve and restore running state

Record whether the application/server/dev process was running before upgrade.

If it was running, stop it before the phase that requires unlocked files and restart the newly deployed version after successful completion. If it was not running, do not start it merely because an upgrade occurred unless the project explicitly defines that behavior.

A restart request is not proof of successful startup. After `Start-Process`, wait briefly and verify that the new process is still alive or, preferably, perform a project-specific health check.

If the upgrade itself succeeded but restoration of the previous runtime state fails, report `WARNING` when the application files are valid and manual start remains possible. Use `FAILED` if the project contract requires the runtime to be operational for deployment to be considered complete.

Where a project has `run.cmd`, define its semantics clearly. A useful standard is: if the application is already running, stop it and start the current build again; otherwise start it normally. The upgrader may reuse the same authoritative start/stop implementation rather than duplicating runtime knowledge.

## 12. Upgrade locking

Two concurrent upgrades of the same installation must not run.

Use a repository- or project-specific lock with enough information to distinguish an active owner from a stale lock. A stale lock left after a crash should be recoverable safely.

Do not use a global lock name that prevents independent checkouts from being upgraded when they do not share artifacts.

## 13. Dependencies are the upgrader's responsibility

A new computer should not require a scavenger hunt through README instructions before `upgrade.cmd` works.

Check all required tools and versions. Install missing dependencies automatically where the project policy permits it, using documented stable package identifiers and the latest stable, well-documented third-party version unless the project intentionally pins a version.

Examples include Git, .NET SDK, Node.js/npm, CMake, compilers, package managers, generated launchers, native dependencies, or project-specific runtime assets.

After installing a dependency, verify the actual executable/version. Installer exit code alone is insufficient.

When replacing a legacy dependency, first validate the project successfully on the new dependency. Only then remove the old dependency. Failure of optional cleanup after successful validation should normally be a warning, not destruction of the working environment.

External installers may be busy. For MSI-based tooling, wait for Windows Installer to become idle and use bounded retries rather than racing another installation.

Never force-kill unrelated build/runtime infrastructure just to make an upgrade pass. Prefer supported shutdown commands such as `.NET build-server shutdown`.

## 14. Third-party host applications

Some projects integrate with or deploy into another application (for example Total Commander).

Do not assume a third-party updater preserves its existing installation directory. Detect the exact current executable directory, pass it explicitly using the vendor's documented mechanism, and verify the expected new version in that same directory.

Before upgrading a host that may rewrite settings, back up the specific project-relevant configuration. After the host upgrade, reapply and verify required settings before restarting it.

Managed host configuration must behave as a set, not an append-only list. Scan all matching project-owned entries, preserve exactly one canonical entry, remove stale duplicates, preserve unrelated user entries, and verify the final count.

## 15. Build, test, package and deploy are separate phases

Never build directly into a live installation directory.

Preferred lifecycle:

```text
CONFIGURE -> RESTORE -> BUILD -> TEST -> VERIFY ARTIFACTS -> STAGE PACKAGE -> DEPLOY -> VERIFY DEPLOYMENT
```

Build output belongs in the build tree. Package preparation belongs in an isolated staging directory. Only verified artifacts are copied into the live destination during DEPLOY.

Do not use CMake/MSBuild `POST_BUILD` actions to copy directly into live `dist` or a host plugin directory.

If the live target may be locked, diagnose the owner, retry for a bounded period where appropriate, and fail with a useful lock message rather than partially replacing files.

Verify every required artifact before deployment. A successful compiler exit code does not prove that all required executables, DLLs, plugins, generated files, or assets exist.

When stale build state has previously caused false results, prefer deterministic cleanup/recreation of known build output over increasingly complicated incremental repair logic.

## 16. Native commands in PowerShell

For native executables, exit code is authoritative. stderr is not synonymous with failure.

Git, compilers, package managers, and build systems routinely write warnings or progress to stderr. Windows PowerShell 5.1 can surface native stderr as `ErrorRecord` objects, especially when `$ErrorActionPreference = 'Stop'` and pipelines/redirection are involved.

Use a dedicated native-command helper. Capture `$LASTEXITCODE` immediately after the command being tested, before another native command can overwrite it. Preserve enough stdout/stderr for diagnosis while classifying warnings separately from failures.

Avoid ambiguous PowerShell argument-array binding. A wrapper that accidentally invokes bare `git.exe` instead of `git fetch origin` can produce only Git's usage screen. Use explicit parameter names and test the exact invocation.

## 17. Logging is mandatory

Every upgrade must produce one single-run diagnostic log. The preferred location is:

```text
<repository>\logs\upgrade.log
```

A legacy project may use repository-root `upgrade.log`, but new/modernized projects should standardize on `logs\upgrade.log` so operational logs stay grouped together.

Truncate/replace the current upgrade log at the beginning of each run. Do not append new runs indefinitely.

The log must be sufficient for remote diagnosis without screenshots. Include at least:

- updater revision;
- date/time;
- repository source path and active path if `pushd` changed it;
- target branch;
- starting commit;
- synchronized/build commit;
- relevant dependency versions;
- phases and commands/results needed to diagnose failure;
- warnings;
- final status.

Use stable final markers:

```text
STATUS: SUCCESS - phase=COMPLETE
STATUS: WARNING - phase=COMPLETE
STATUS: FAILED - phase=<PHASE>
```

The process exit code and final status must agree. `SUCCESS` and `WARNING` return zero unless a project explicitly defines otherwise; `FAILED` returns non-zero.

Console colors are presentation only: gray/default for normal information, yellow for warning/action required, red for failure, green for successful completion. Logs must remain understandable without color.

## 18. Interactive operations

Avoid unnecessary prompts. An updater should normally be unattended except for unavoidable UAC/vendor installer confirmation or a genuinely destructive decision that cannot be inferred safely.

Before waiting for input, print a complete `ACTION REQUIRED` message explaining exactly what is needed.

Nested child processes can buffer/reorder output when stdout/stderr is captured. Do not let an interactive prompt appear before the status text that explains it. Prefer direct console key input for small helpers and show visible progress/liveness during long external operations.

## 19. Stable upgrade phases

Use named phases rather than only step numbers. Recommended names include:

```text
SELF-UPDATE
BOOTSTRAP
REPOSITORY
DEPENDENCIES
STOP-RUNTIME
CONFIGURATION
MIGRATION
CLEAN
RESTORE
CONFIGURE
BUILD
TEST
DIST
DEPLOY
VERIFY
RESTART
COMPLETE
```

Project-specific phases are fine, but failure output must identify the active phase.

## 20. Failure semantics

On failure:

- stop at the failed phase;
- do not deploy partial/unverified artifacts;
- preserve runtime/user data;
- preserve useful diagnostics;
- print a concise red error;
- end the log with `STATUS: FAILED - phase=<PHASE>`;
- return non-zero.

If the application was stopped before a build that later fails, restoration policy must be explicit. Restarting the old known-good runtime can be appropriate when its deployed files were never modified. Do not restart blindly after a partial deployment.

## 21. Success semantics

On success:

- verify final artifacts/deployment;
- restore previous running state where applicable;
- print important resulting paths/version;
- report warnings separately;
- end with `STATUS: SUCCESS` or `STATUS: WARNING`;
- return zero.

A warning means the requested upgrade completed but a non-fatal condition remains. Do not use warning as a euphemism for an unusable installation.

## 22. Idempotence

Immediately running `upgrade.cmd` again must be safe.

The second run must not:

- dirty tracked scripts because of line endings;
- duplicate host configuration;
- repeat a completed migration destructively;
- overwrite user configuration;
- delete runtime data;
- require manual cleanup;
- fail because a previous temporary/lock file was left behind;
- reinstall dependencies unnecessarily;
- start an application that was originally stopped.

Idempotence is an acceptance criterion, not an optional optimization.

## 23. Known traps and proven fixes

This buglist is mandatory reading before creating or changing an updater.

### Bootstrap / interpreter boundary

- **Running `.cmd` overwrites itself.** Symptom: one run prints messages from two updater generations or jumps into impossible labels. Root cause: `cmd.exe` continues reading a file Git replaced. Fix: execute the current updater/runner from `%TEMP%`; keep the repository launcher minimal.
- **LF-only temporary batch file.** Symptom: `The system cannot find the batch label specified`. Root cause: batch content extracted from Git is LF-only. Fix: explicitly normalize executable temporary `.cmd` files to CRLF.
- **Deep CMD/PowerShell nesting.** Symptom: quoting, trailing slashes, environment expansion, and exit codes fail unpredictably. Fix: one bootstrap layer and one authoritative runner.
- **PowerShell `param()` bootstrap collision.** Symptom: unexpected `Supply values...` prompt or binding error. Fix: keep bootstrap transport simple; environment variables are often safer than complex mandatory parameter binding.
- **Trailing backslash in quoted path.** Symptom: a valid repository path is split or quoted incorrectly in a child interpreter. Fix: canonicalize and trim the trailing separator before transport.
- **Interactive child prompt hidden by buffering.** Symptom: updater appears frozen or asks a question before explaining it. Fix: print ordered `ACTION REQUIRED` text before reading input and avoid redirected interactive helpers.

### Git / working tree

- **`dubious ownership` mistaken for “not a repository”.** Symptom: bootstrap attempts a nested clone/init. Fix: detect Git's actual diagnostic and apply a narrowly scoped exact `safe.directory` exception.
- **Global `safe.directory=*`.** Symptom: updater works but permanently disables an important Git ownership protection. Fix: use process-scoped exact-repository configuration.
- **CRLF vs Git blob false mismatch.** Symptom: synchronized `upgrade.cmd` is repeatedly considered modified. Fix: use Git normalization semantics, not raw bytes.
- **Managed stash still leaves bootstrap dirty.** Symptom: stash reports success, then checkout/pull says local changes would be overwritten. Root cause: updater/line-ending materialization changed the bootstrap file. Fix: treat bootstrap files as authoritative remote state and preserve genuine tracked edits separately.
- **`git stash -u` captures runtime data.** Fix: do not include untracked files unless their lifecycle is explicitly designed.
- **`git clean -fd` deletes valuable local data.** Fix: clean only known generated paths.
- **Wrong branch is built.** Fix: centralize branch selection and verify final `HEAD == origin/<target>`.
- **Remote URL drift.** Fix: verify/set expected origin where repository policy requires it.
- **Old launcher cannot reach new updater.** Fix: keep self-update protocol intentionally simple and backward-compatible.

### Network-drive / cache

- **`cd /d` or child tools fail on UNC.** Fix: use `pushd`/`popd` in CMD and pass the resulting active path to tools that require a drive path.
- **Package cache on network share is slow or lock-prone.** Fix: explicitly choose a local cache for that tool, or a repository `.cache` when portability is more important and the tool is proven safe there.
- **Persistent project state silently moves to `%LOCALAPPDATA%`.** Fix: define where project-owned persistent state belongs and migrate/remove obsolete cache locations intentionally.

### Process / runtime

- **Blindly killing `node.exe`, `dotnet.exe`, etc.** Symptom: unrelated applications die. Fix: identify project-owned processes by path/command line/tree and stop only those.
- **Process exits between discovery and kill.** Fix: treat “already exited” as success.
- **Start command returns but application immediately dies.** Fix: verify the process after a short delay or perform a health check.
- **Missing alternate executable name treated as fatal.** Fix: enumerate first; stop only processes that exist.
- **Application was stopped before upgrade but starts afterward.** Fix: record and restore prior running state, not a guessed desired state.
- **Application was running but upgrade leaves it stopped.** Fix: restart after successful deployment and verify startup.
- **Locked `esbuild.exe`/DLL/plugin after process stop.** Fix: verify the actual target file can be opened/replaced before package/deploy; diagnose remaining owners instead of killing unrelated processes.

### Build / deploy

- **Build writes directly into live `dist`.** Symptom: sharing violation from host/AV/indexer. Fix: build and stage away from live deployment.
- **Live `dist` is accidentally used as package staging.** Symptom: DIST fails before DEPLOY because a host owns the plugin. Fix: stage under build output, deploy only after verification.
- **Host process still owns DLL/WDX.** Fix: identify all host processes that can own the file, not only the project's worker.
- **Partial deployment.** Fix: verify all artifacts before touching the live target.
- **Build exits zero but artifact is missing.** Fix: explicit artifact verification.
- **Stale build state causes false behavior.** Fix: deterministic cleanup of known generated/build directories where necessary.

### Dependencies / installers

- **Installer succeeds but executable is unavailable in current shell.** Fix: refresh PATH/probe known installation paths and verify the tool.
- **New dependency installed, old dependency removed, then project validation fails.** Fix: validate on the new dependency before removing the old one.
- **Windows Installer already busy.** Fix: wait with timeout and bounded retry.
- **Third-party updater installs a second copy elsewhere.** Fix: capture and explicitly pass the existing installation directory; verify the new version there.
- **Third-party updater resets host configuration.** Fix: preserve project-relevant settings, reapply them, and verify before restart.
- **Dependency download is partial/corrupt.** Fix: verify required extracted files/content, not only downloader exit code.

### Configuration / data

- **Default config overwrites user config.** Fix: create defaults only when missing; migrate existing configuration.
- **Managed host entries duplicate on every upgrade.** Fix: canonicalize the entire matching set and verify exactly one project-owned entry remains.
- **Environment variable text becomes a literal relative path.** Fix: resolve paths in one authoritative layer and validate absolute paths where required.
- **Migration runs twice.** Fix: migrations must be versioned/idempotent.
- **Runtime configuration/log/database accidentally becomes tracked.** Fix: define data boundaries and `.gitignore` before deployment logic.

### PowerShell / native commands

- **stderr treated as failure.** Symptom: harmless Git CRLF warning aborts upgrade. Fix: native exit code is authoritative.
- **`$ErrorActionPreference='Stop'` turns native stderr into an exception.** Fix: isolate native execution in a helper and inspect exit code explicitly.
- **`$LASTEXITCODE` is read too late.** Fix: capture it immediately.
- **Argument-array wrapper invokes the wrong command.** Symptom: generic Git usage page. Fix: explicit argument handling and tested invocation.
- **Localized/garbled compiler output changes logic.** Fix: do not parse human-localized console prose for success/failure when an exit code or stable machine-readable result exists.

### Logging / status

- **Failure has no final status.** Fix: top-level exception/finally handling must always write the final marker.
- **Log says failed but process exits zero, or vice versa.** Fix: one authoritative final result drives both log marker and exit code.
- **New run appends to old diagnostics.** Fix: single-run log replacement.
- **Console is repeatedly cleared by child phases.** Fix: clear once at interactive entry only; never erase diagnostics during recovery/internal execution.

## 24. Three-strike rule for updater debugging

Do not keep applying variants of the same unsuccessful fix.

If a problem has produced three materially similar failed attempts:

1. stop experimentation;
2. preserve the failing log and exact reproduction;
3. roll back to the last known-good state before the failed experiments;
4. search authoritative documentation and strong community/maintainer guidance for the specific failure class;
5. implement a different evidence-based approach;
6. add the discovered failure mode and prevention to this document.

This rule exists because bootstrap, Git, quoting, CRLF, installer, and network-drive bugs can easily become circular “fixes” that merely move the failure to another interpreter boundary.

## 25. Patterns explicitly avoided

Do not reintroduce without a strong documented reason:

- large monolithic label-heavy batch updaters;
- self-overwriting running scripts;
- repeated CMD/PowerShell interpreter chains;
- global `safe.directory=*`;
- raw CRLF/blob byte comparisons as Git state checks;
- arbitrary stderr-as-failure;
- blind killing of shared runtime processes;
- build-to-live-directory;
- broad untracked-file cleanup;
- silent destruction of local tracked changes;
- hidden machine-specific paths;
- append-only managed host configuration;
- dependency removal before replacement validation;
- assuming “process started” means “application healthy”.

## 26. Recommended project files

```text
upgrade.cmd           bootstrap launcher
upgrade.ps1           authoritative runner where appropriate
install.cmd           optional fresh-install entry when install != upgrade
run.cmd               optional authoritative start/restart entry
logs/upgrade.log      generated, ignored, single-run diagnostics
.gitattributes        explicit Windows script line endings
.gitignore            logs/runtime/cache/build exclusions
CHANGELOG.md           application changes
UPGRADE.md             this protocol + project-specific additions if needed
```

Do not force every repository to have every file. Preserve the architectural responsibilities even when a small project combines them.

## 27. Minimum acceptance matrix

Before declaring an upgrader stable, deliberately test at least:

- clean repository, application stopped;
- clean repository, application running;
- immediate second upgrade run;
- no remote update;
- real remote update;
- old local updater reaching the current runner;
- repository path containing spaces;
- mapped network drive;
- UNC path where applicable;
- Git `dubious ownership` condition;
- tracked local modification;
- staged local modification;
- valuable untracked runtime file;
- harmless native stderr warning;
- missing dependency on a fresh machine;
- dependency installation followed by verification;
- build/test failure before deploy;
- required artifact missing despite build completion;
- locked deployment target;
- graceful shutdown timeout;
- project-owned shared-runtime process plus an unrelated process of the same executable type;
- application restart success and immediate-startup failure;
- existing user configuration/log/database;
- already-applied migration;
- stale updater lock after simulated crash;
- repeated host integration producing exactly one managed entry;
- optional third-party host update preserving installation directory and required settings;
- correct final log marker and exit code for SUCCESS, WARNING, and FAILED.

For projects supporting fresh bootstrap, additionally test a new Windows installation/folder containing only the permitted bootstrap file(s).

## 28. Project-specific additions

A repository may extend this protocol with project-specific rules: required branch, repository URL, runtime process identification, dependencies, build/test commands, deployment destinations, host integrations, configuration paths, migrations, and health checks.

Project-specific rules may strengthen this protocol but should not silently weaken data safety, Git safety, network-drive support, logging, idempotence, or self-update guarantees. Any intentional exception should be documented next to the reason.

## 29. Buglist maintenance rule

Whenever a new updater defect is discovered, do not only patch the code.

Add to this document:

1. failure mode;
2. recognizable symptom;
3. root cause;
4. proven prevention/fix;
5. acceptance test when practical.

The objective is simple: the same class of upgrade bug should be solved once for the whole project family.

## 30. Design principle

The upgrader is part of the application.

Treat `upgrade.cmd` and its runner with the same engineering discipline as production code: version it, test it on realistic machines and network paths, protect user data, verify every destructive boundary, log enough to diagnose failures remotely, and keep the bootstrap path simpler than the application it maintains.
