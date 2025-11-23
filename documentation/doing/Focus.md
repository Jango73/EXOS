# Focus handling plan

## Objective
- Introduce explicit focus (desktop + process) and a global input message queue so only the foreground process receives keyboard events; eliminate broadcast side effects.

## Plan
1. [x] **Kernel focus state (KernelData.c:Kernel)**
   - Add focused desktop pointer and focused process pointer with getters/setters (header export as needed). Keep names consistent with existing Kernel fields and follow ordering (#defines, typedefs, inlines, externs).
   - Add a global input message queue instance in Kernel for device input ingress (keyboard first). Document ownership and init path.
2. [x] **Initialization wiring**
   - Initialize focus pointers during Kernel init to the default desktop/process; ensure the global queue is constructed/reset there.
   - Provide safe defaults if no desktop/process is focused yet.
3. [x] **Input enqueue path**
   - Route keyboard (and other input) events into the global queue instead of broadcasting to all tasks. Preserve existing message format; ensure logging and SAFE_USE usage stay correct.
4. [x] **Message retrieval**
   - Update PeekMessage/GetMessage so that a task checks the global queue only when its process currently has focus; otherwise, fall back to its own queue. Ensure synchronization/locking around the shared queue.
   - Avoid duplicate delivery between global and per-task queues; clarify precedence.
5. [ ] **Focus on process creation**
   - When a process is created, if its desktop already has focus, automatically give that process focus. Handle handoff when the process exits to restore prior focus (likely shell).
6. [ ] **Focus change API**
   - Provide functions to change focused desktop/process explicitly (console switches, future GUI focus). Consider whether changing the focused desktop should also adjust the focused process or clear it.
7. [ ] **Documentation**
   - Update documentation/Kernel.md to describe focus tracking, the global input queue, and the message dispatch rules.
8. [ ] **Validation**
   - Build/run: start shell, launch userland app, type input, exit app; verify shell does not receive buffered keystrokes. Keep test under 15s per guidelines.
