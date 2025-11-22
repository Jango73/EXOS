# Event-Driven Task Messages

1. [ ] Inventory current message system and keyboard path
   - Map existing window message structures/functions (SendMessage, PostMessage, PeekMessage, GetMessage, queues, init/destroy, synchronization).
   - Trace keyboard flow (ISR/driver → dispatch → window) to locate injection points and polling loops (e.g., shell).
2. [ ] Define unified MessageQueue abstraction
   - Specify common queue struct and ops (init/destroy, push/pop/peek, wait/wakeup, capacity/limits) usable by both windows and tasks.
   - Align message format for task delivery (key codes, flags, optional sender).
3. [ ] Extend API contract to handle NULL handles for tasks
   - Allow SendMessage/PostMessage/PeekMessage/GetMessage to accept NULL as “target task” (no window).
   - Adjust validation to accept NULL handles and route accordingly.
4. [ ] Attach MessageQueue to Task lifecycle
   - Add MessageQueue field to Task; create during task creation and destroy at teardown.
   - Reuse the same queue implementation for windows (via composition/wrappers) to maximize shared code.
5. [ ] Implement message injection to tasks
   - Update dispatch so NULL handle posts into the target task’s queue (current task or explicit task when provided).
   - Adapt keyboard ISR/driver path to post key events to tasks without windows (e.g., shell) using PostMessage with NULL handle.
   - Ensure SAFE_USE/SAFE_USE_VALID_ID use on kernel object pointers where required.
6. [ ] Consume messages in tasks
   - Extend PeekMessage/GetMessage to read from the calling task’s queue when handle is NULL.
   - Update shell and other windowless apps to replace keyboard polling loops with PeekMessage/GetMessage handling key messages.
7. [ ] Synchronization and wakeup
   - Ensure queue ops are thread-safe (locks/irq masking) and that GetMessage can block; wake the task when a message is enqueued.
   - Keep PeekMessage non-blocking; confirm scheduler interaction for wakeups is correct.
8. [ ] Compatibility, tracing, and docs
   - Verify window message delivery remains unchanged; add DEBUG logs on new task message paths for diagnostics.
   - Add/update documentation in documentation/Kernel.md describing shared message queues, NULL-handle semantics, and task lifecycle notes.
