# Upgrade Protocol

This document is the master upgrade standard for Windows repositories in this project family. It consolidates proven practices from FolderHeatMap, VoicePrompter, VoicePrompterBridge / Socket Universe Bridge, VirtualMonitorsUniverse, Companion modules, WindowsTerminalFlow, and related Wipe Codes projects.

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
upgrade.cmd -> temporary launcher context -> current temporary upgrade.ps1 -> upgrade lifecycle
```

`upgrade.cmd` is a bootstrap launcher, not the main application. Keep it as small and stable as practical. Substantial logic belongs in `upgrade.ps1`.

The repository copy of `upgrade.cmd` should do as little as possible before it makes itself independent from the working tree. A proven pattern is to copy the launcher to a unique `%TEMP%` path first, then let the temporary copy perform repository discovery, fetch/self-update and the handoff to the current `upgrade.ps1`.

This matters because the later repository synchronization may replace `upgrade.cmd` itself. The repository copy must therefore never depend on reading additional lines after a child process that can reset or replace the tracked tree has started.

The launcher may resolve the repository path, make UNC paths usable, bootstrap Git when project policy allows it, fetch the target branch, extract the current runner to `%TEMP%`, establish narrowly scoped environment state, invoke PowerShell, remove the temporary runner, and return exactly its exit code.

Do not build a large label-heavy batch updater unless a project has a compelling documented reason. Do not create repeated `CMD -> PowerShell -> CMD -> PowerShell` interpreter chains.

Keep an application version in `x.xx` form and, where useful for diagnostics, a separate updater revision.

## 3. Self-update is phase zero

The updater in the target branch is authoritative. An old local updater must be able to reach and execute the current upgrade implementation before performing the real upgrade.

Never overwrite a running script and then rely on that process continuing to read the same file. `cmd.exe` may continue parsing a replaced batch file at a different byte/line position and execute arbitrary fragments from the new version. Symptoms can look unrelated, for example `'not' is not recognized`, `'f' is not recognized`, empty arguments, or jumping into the wrong label after an otherwise successful build.

Therefore the repository copy of `upgrade.cmd` must leave the mutable working tree before any operation that can replace tracked files. Prefer one of these designs:

1. copy the current launcher to a unique `%TEMP%` file immediately and transfer control to that copy before Git synchronization; or
2. use another one-way handoff whose executable script cannot be modified by the repository reset.

The original repository copy must not resume reading later lines after the temporary child returns. A handoff that waits for the child and then continues in the original batch file is unsafe if Git may replace that file while the child runs.

The safest general runner pattern is to fetch the target branch, extract `upgrade.ps1` to a unique temporary path, and execute that copy. The PowerShell runner may then synchronize/reset the repository without changing the code currently executing.

If the project still materializes a remote `.cmd` and executes it, explicitly normalize that temporary executable copy to CRLF before launch. Git blob content may be LF-only even when the working-tree policy is CRLF.

### Self-update compatibility contract

The self-update path is the recovery path for old installations and must remain backward-compatible.

Before changing an internal bootstrap argument, environment variable, label contract, or handoff convention, determine how currently deployed older launchers call the new updater. A new launcher must accept the handoff forms that supported older launchers still use, or provide a deliberate compatibility shim.

Do not assume that updating `upgrade.cmd` in the repository automatically updates the already-running launcher. The old launcher is exactly the component performing the transition to the new one.

When repository identity/path must survive the transition, carry it redundantly when practical: use the explicit command-line argument expected by the current protocol and a stable environment-variable fallback. Validate the resolved value before using it. Never allow an empty repository path to silently fall through into Git/bootstrap operations.

If a temporary launcher is created through `git show` or another transport, verify that the destination path variables used by the conversion/copy command are actually defined in that child process. Do not rely on delayed expansion or a parent-local variable magically becoming an environment variable visible to PowerShell.

Keep internal handoff names stable. If they must change, retain aliases for previously shipped forms until every supported older launcher can reach a newer runner without them.

### Required self-update acceptance tests

A self-update implementation is not considered stable until all of these pass:

- current launcher -> current runner;
- at least the immediately previous shipped launcher -> current runner;
- an older launcher whose local `upgrade.cmd` differs from `origin/<branch>` -> current runner;
- self-update where `git reset --hard` replaces the repository copy of `upgrade.cmd` while the temporary runner is active;
- repository path containing spaces;
- mapped network path;
- immediate second run after successful self-update.

A build that succeeds but is followed by stray CMD errors is still an updater failure. Success is reached only when control returns cleanly to the caller with the correct exit code and no residual batch execution.

## 4. Repository paths: local, mapped and UNC are all valid

Repository data may live on a network drive. This is a supported configuration, not an exceptional error case.

Never hard-code a drive letter, checkout location, username, or workstation-specific path. Resolve the repository from `%~dp0` or an equivalent canonical source.

For batch launchers prefer:

```bat
pushd "%REPO_DIR%"
```

over `cd /d`. `pushd` works with ordinary paths and maps UNC paths to a temporary drive letter for tools that cannot operate directly on UNC paths. Always pair it with `popd`.

Normalize paths at interpreter boundaries. In particular, trim an unnecessary trailing backslash before transporting a quoted path through CMD/PowerShell arguments or environment variables.

When passing a critical path across a CMD/PowerShell/self-update boundary, validate it immediately on the receiving side. Do not continue if it is empty. If the path is needed after a self-replacement handoff, preserve it in a stable form that survives the transition.

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

If the target contains only the bootstrap `upgrade.cmd`, the bootstrap must execute from outside that target (normally `%TEMP%`) before deleting/replacing the target copy and cloning into the exact intended directory. Do not clone a nested repository simply because the initial launcher is standing inside an otherwise empty target folder.

After clone, verify at least `.git`, the expected remote/repository identity, the authoritative updater file, and the target branch before handing off.

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

Authoritative bootstrap/updater files may legitimately differ locally during self-update, bootstrap handoff, or line-ending materialization. If project policy treats those files as disposable authoritative bootstrap state, exclude only those explicitly named updater files from the local-edit rejection check, then synchronize them deterministically from the target branch. Do not weaken dirty-tree protection for the rest of the repository.

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

When the repository or deployment target is on SMB/NAS/mapped storage, do not assume whole-directory rename/move semantics are reliable. Prefer a verified staging tree, backup of the current deployment, file-by-file copy with bounded retries, post-copy verification, and rollback from the backup on failure. Directory creation/removal operations may also need explicit retry/error handling on network storage.

When stale build state has previously caused false results, prefer deterministic cleanup/recreation of known build output over increasingly complicated incremental repair logic.

## 16. Native commands in PowerShell

For native executables, exit code is authoritative. stderr is not synonymous with failure.

Git, compilers, package managers, and build systems routinely write warnings or progress to stderr. Windows PowerShell 5.1 can surface native stderr as `ErrorRecord` objects, especially when `$ErrorActionPreference = 'Stop'` and pipelines/redirection are involved.

Use a dedicated native-command helper. Capture `$LASTEXITCODE` immediately after the command being tested, before another native command can overwrite it. Preserve enough stdout/stderr for diagnosis while classifying warnings separately from failures.

Avoid ambiguous PowerShell argument-array binding. Do not use parameter names that collide with automatic variables such as `$args`. A wrapper that accidentally invokes bare `git.exe` instead of `git fetch origin` can produce only Git's usage screen. Prefer an explicit parameter such as `$ArgumentList`, call it with named parameters, and test the exact invocation.

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

For interactive Windows launchers, start with `cls` once at the beginning of the user-invoked `upgrade.cmd`. Do not repeatedly clear the screen during later phases because the visible diagnostics are valuable.

Use a known console/output encoding for both CMD and PowerShell/native tools. UTF-8 is the preferred modern choice where the toolchain supports it. Verify localized output is readable; mojibake is an updater defect because it can hide the actual diagnostic.

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

If the same class of updater failure has already been attempted three times without success, stop producing variations. Preserve the failing log and the last known-good state, research authoritative/maintainer guidance, then implement a different evidence-based design. Add the newly understood failure mode to this document before considering the issue closed.

## 21. Success semantics

On success:

- verify final artifacts/deployment;
- restore previous running state where applicable;
- print important resulting paths/version;
- report warnings separately;
- end with `STATUS: SUCCESS` or `STATUS: WARNING`;
- return zero;
- return cleanly to the invoking shell without stray commands/errors from an older batch context.

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
- start an application that was originally stopped;
- enter a different self-update path merely because the local updater was synchronized on the first run.

Idempotence is an acceptance criterion, not an optional optimization.

## 23. Notorious updater failures already solved

These failure classes have already cost development time and must not be rediscovered:

- treating a fresh directory containing only `upgrade.cmd` as an existing checkout and failing because `.git` is absent;
- interpreting `safe.directory` / dubious-ownership failures as a missing repository and accidentally bootstrapping a nested clone;
- running Git synchronization from the repository copy of `upgrade.cmd`, replacing that file, then allowing `cmd.exe` to continue reading the changed file;
- assuming that a successful child build means the updater succeeded even though the parent batch later executes garbage fragments such as `'not' is not recognized` or `'f' is not recognized`;
- changing internal self-update arguments without retaining compatibility for older launchers, causing `ERROR: Unknown upgrade option` before the new updater can take over;
- losing the repository path across a launcher self-replacement boundary and reaching the new updater with an empty path;
- invoking PowerShell with `%TEMP%` path variables that exist only as delayed-expansion batch variables rather than real environment variables, producing empty-path `ReadAllText` / `WriteAllText` failures;
- executing temporary `.cmd` content taken from Git without CRLF normalization;
- rejecting authoritative bootstrap files as user dirty-tree changes during the very update that is supposed to replace them;
- using `$args` or another PowerShell automatic-variable name for a native-command wrapper parameter and accidentally invoking bare `git.exe`;
- treating native stderr as fatal while the native process exit code is zero;
- assuming whole-directory rename/move on SMB/network storage is a safe deployment primitive;
- deleting the old live deployment before staged artifacts have been verified and a rollback path exists;
- allowing console codepage/encoding mismatch to turn localized diagnostics into unreadable mojibake;
- failing to run the updater immediately a second time to prove idempotence.

Whenever a new project introduces `upgrade.cmd`, review this section before implementation, not after the first failures.
