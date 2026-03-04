
# Process Control Hotkeys Plan

## Goal

Provide a generic hotkey mechanism that can interrupt long-running commands and control processes (kill/pause) through a message-interception based design.

## Scope

- Configurable hotkeys from configuration file.
- Generic action dispatch from key bindings.
- Process-aware behavior for focused process.
- Kernel-safe command interruption (cooperative cancel).
- Non-kernel process pause/resume and kill.

## Plan

### 1. Introduce a generic hotkey module

- Add `kernel/include/input/Hotkey.h`.
- Add `kernel/source/input/Hotkey.c`.
- Responsibilities:
  - Parse configuration entries (`Hotkey.%u.Key`, `Hotkey.%u.Action`) through TOML access.
  - Parse key expressions such as `control+c` into `(modifiers, virtual key)`.
  - Match only on key-down events with repeat protection.
  - Dispatch through a generic action table instead of local `if` chains.

### 2. Add process/task control interception through messaging

- Add `kernel/include/process/Process-Control.h`.
- Add `kernel/source/process/Process-Control.c`.
- Define internal control messages for process control:
  - `ETM_INTERRUPT`
  - `ETM_PROCESS_KILL`
  - `ETM_PROCESS_TOGGLE_PAUSE`
- Add one interception point in task messaging (`Task-Messaging.c`) before queue insertion:
  - If message is a control message, process immediately and consume it.
  - Otherwise continue normal queue routing.

### 3. Implement action `kill_process`

- Resolve focused process via `GetFocusedProcess()`.
- Behavior:
  - If focused process is the kernel process:
    - Do not kill kernel process.
    - Request interruption of the current running command (if any).
  - Otherwise:
    - Kill focused process with existing process termination path.

### 4. Implement action `pause_process` (toggle)

- Add dedicated process pause state flag in process metadata.
- Behavior:
  - If focused process is the kernel process: no operation.
  - Otherwise: toggle paused state.
- Scheduler integration:
  - Skip scheduling tasks owned by paused processes.
  - Resume is achieved by clearing pause flag.

### 5. Implement cooperative interruption for long kernel commands

- Add a generic interruption API (process-control side), for example:
  - `ProcessControlRequestInterrupt(Process)`
  - `ProcessControlIsInterruptRequested(Process)`
  - `ProcessControlConsumeInterrupt(Process)`
- Add lightweight cancellation checkpoints in long loops (`dir`, recursive listing, similar command loops).
- Keep this generic through shared helper(s), not duplicated per command.

### 6. Integrate hotkey evaluation in keyboard input flow

- In keyboard common path (`Keyboard-Common.c`), after key decode and before normal routing:
  - Call hotkey handler.
  - If hotkey is consumed, stop propagation to focused process queue.
  - Otherwise continue existing input routing.

### 7. Documentation and configuration example

- Update `documentation/Kernel.md` with:
  - Hotkey model.
  - Supported actions and behavior rules.
  - Focused-process semantics.
- Configuration example:

```toml
[[Hotkey]]
Key = "control+c"
Action = "kill_process"

[[Hotkey]]
Key = "shift+z"
Action = "pause_process"
```

## Implementation order

1. Process-control subsystem and message interception.
2. Pause/resume scheduler policy by process flag.
3. Cooperative interruption API and `dir` integration.
4. Hotkey module and configuration parsing.
5. Keyboard integration.
6. Documentation updates and validation.
