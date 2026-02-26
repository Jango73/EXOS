# Smoke x86-64 Intermittent Failure Report (2026-02-26)

## Scope
This report captures only direct observations made during interactive smoke test runs in this session.
It is based on terminal outputs observed at run time.
No post-run log file evidence is available for this report.

## Context
- Test command used for full smoke: `bash scripts/4-1-smoke-test.sh`
- Test command used for x86-64-only smoke: `bash scripts/4-1-smoke-test.sh --only x86-64`
- Build mode used by smoke script: debug, ext2
- Target architectures exercised: x86-32 and x86-64

## Observed Sequence
1. Full smoke run was started.
2. x86-32 path completed build, image generation, boot, and command sequence.
3. x86-32 executed smoke commands including:
   - `sys_info`
   - `dir`
   - `/system/apps/hello --no-pause`
   - file operations on `/fs/n0p0` and `/fs/n1p0`
   - `scripts/test.e0`
   - `/system/apps/netget --smoke-local /temp/index.html` with HTTP 200 observed
   - `package run test`
   - `shutdown`
4. Same full smoke run then switched to x86-64.
5. x86-64 performed clean build and image generation successfully.
6. x86-64 reached `Starting QEMU for x86-64`.
7. No further expected smoke progression was observed from that run.
8. Script ended in timeout/failure state with:
   - `Timed out waiting for expected log: [InitializeKernel] Shell task created`

## Additional Runtime Observation During Failed x86-64 Run
- While the run was stuck, process list showed:
  - `scripts/run.sh --arch x86-64 --fs ext2 --debug`
  - active `qemu-system-x86_64` process
- During that same stuck period, kernel/debug log files queried from terminal returned no usable output in that snapshot.

## Second Attempt (x86-64 Only)
1. A new run was started with `bash scripts/4-1-smoke-test.sh --only x86-64`.
2. x86-64 build and image generation completed.
3. x86-64 reached QEMU execution.
4. Smoke commands started and visible output confirmed at least:
   - `Running command: sys_info`
   - `Running command: dir`
   - `Running command: /system/apps/hello --no-pause`
5. This second attempt was manually interrupted by user before completion.

## What Is Proven by These Observations
- x86-64 smoke behavior is non-deterministic across attempts in the same environment.
- A failure mode exists where x86-64 reaches QEMU launch but never reaches the expected shell-ready log milestone.
- The same path can also progress normally on another attempt without code changes between attempts.

## Practical Failure Signature
Use this as the canonical symptom for this incident:
- `Timed out waiting for expected log: [InitializeKernel] Shell task created`

## Candidate Weakness Class (Inference from Behavior)
This section is inference from observed behavior, not direct proof.
- Intermittent early-boot race or initialization ordering issue in x86-64 path.
- Potential fragile dependency on timing, device init order, or event delivery before shell task creation.

## Reproduction Notes
- Re-run command repeatedly:
  - `bash scripts/4-1-smoke-test.sh --only x86-64`
- Watch for divergence between:
  - normal progression into smoke commands
  - timeout before `[InitializeKernel] Shell task created`

## Limits of This Report
- No preserved log file payload is included.
- No crash dump, backtrace, or fault frame is included.
- This report records observed external behavior and sequencing only.
