# Context Switch Flow in 32-bit Linux

This document describes the complete flow of a context switch in a
32-bit Linux kernel (i386), from an interrupt to the execution of the
new task. The explanation focuses on stack management and task
transitions, while respecting the System V ABI (arguments passed via the
stack). Each step is detailed with pseudo-code in assembly or C for
illustration.

## 1. Interrupt (IRQ) → Entry into the ISR handler

When an interrupt (e.g., IRQ0 for the timer) occurs, the CPU
automatically pushes a return frame (IRET frame) onto the current stack:
the user stack if the task was in user mode (ring 3), or the kernel
stack if it was already in kernel mode (ring 0). This frame contains:

-   EFLAGS: CPU flags (4 bytes).
-   CS: code segment (4 bytes, indicates ring 0 or 3).
-   EIP: the address of the next instruction (4 bytes).

The CPU then jumps to the entry of the interrupt vector defined in the
IDT table (e.g., ISR0 for IRQ0). The handler immediately switches to the
kernel stack (via TSS.esp0) to work in kernel mode.

``` asm
# Save general-purpose registers
pushl %eax
pushl %ebx
pushl %ecx
pushl %edx
pushl %esi
pushl %edi
pushl %ebp
movl %esp, %eax      # Save current ESP
movl TSS.esp0, %esp  # Switch to kernel stack
call do_IRQ          # Call the C handler for the IRQ
```

**Explanation**: The general-purpose registers are saved on the kernel
stack to preserve the state of the interrupted task. The call to
`do_IRQ` (or equivalent) processes the interrupt and may trigger a call
to `schedule()` if a task switch is required (e.g., when a time slice
expires).

## 2. Call to schedule() → switch_to(prev, next)

The `schedule()` function (in C) selects the next task to run and
invokes the `switch_to` macro to switch context between the old task
(`prev`) and the new task (`next`). In 32-bit System V ABI, arguments
are passed via the stack. The `switch_to` macro therefore pushes `prev`
and `next` onto the old task's kernel stack before calling
`__switch_to`.

``` c
#define switch_to(prev, next, last) do {     asm volatile(         "pushl %1
    "       /* Push next onto the stack */         "pushl %2
    "       /* Push prev onto the stack */         "call __switch_to
    " /* Call __switch_to */         "movl %%eax, %0
    " /* Store return value (prev) into last */         : "=a" (last)         : "d" (next), "d" (prev)         : "memory"     ); } while (0)
```

**Explanation**: The `pushl` instructions place the addresses of the
`prev` and `next` task structures onto the old task's kernel stack. The
`call __switch_to` instruction automatically pushes the return address
(pointing to the instruction after `call`) onto this same stack. The
result (the old task) is returned in the `EAX` register and stored into
`last`.

## 3. \_\_switch_to function

The `__switch_to` function (implemented in C, compiled to assembly)
retrieves the `prev` and `next` arguments from the stack
(`[ESP+4]=prev`, `[ESP+8]=next` after the prologue). It saves the old
task's state (ESP, EIP, etc.) into its `thread_struct`, then loads the
new task's state, especially its ESP (kernel stack). If needed, it
updates CR3 to switch the memory space.

``` asm
__switch_to:
    pushl %ebp           # Standard C prologue
    movl 8(%esp), %eax   # prev = [ESP+8] (skip return address)
    movl %esp, thread.esp(%eax)  # Save old ESP into prev->thread.esp
    movl 12(%esp), %edx  # next = [ESP+12]
    movl thread.esp(%edx), %esp  # Load new ESP (next’s kernel stack)
    # Update CR3 if mm_struct changes (flush TLB)
    movl thread.eip(%edx), %ecx  # If first execution, special jump
    popl %ebp
    ret                  # Return to the address on the new stack
```

**Explanation**: Initially, everything happens on the old kernel stack.
The `ret` instruction retrieves the return address from the **new
stack** (since ESP was switched). This requires the new stack to be
pre-populated properly to avoid a crash (see next step). CPU flags
(EFLAGS), FS/GS segments, and other registers are also saved into
`thread_struct`.

## 4. First execution of a new task (empty stack)

For a new task, its kernel stack is empty. Before the switch (in
`copy_process` or `schedule`), this stack must be pre-populated with an
artificial frame to simulate a valid return context. This frame
includes:

-   A fake return address pointing to `ret_from_fork` (a routine that
    finalizes task setup).
-   General-purpose registers (zeroed or initialized with specific
    values).
-   An IRET frame to allow transition to user mode (if required).

``` asm
# New kernel stack (higher to lower addresses, ESP at top)
[ret_from_fork]  # Return address for RET in __switch_to
[0]              # EBP=0 or padding
[edi] [esi] [ebp] [esp] [ebx] [edx] [ecx] [eax]  # General-purpose registers
[EFLAGS] [CS_user] [EIP_user_entry]  # IRET frame for user mode
```

**Explanation**: When `__switch_to` executes `ret`, it retrieves the
address `ret_from_fork` from the new stack. This routine calls
`schedule_tail` (to finalize the task, check signals, etc.), then
executes an `IRET` to switch to user mode (ring 3) by restoring EFLAGS,
CS, and EIP from the IRET frame. For a purely kernel task,
`ret_from_fork` directly jumps to the kernel function entry without
IRET.

## Result after the switch

-   The new task starts execution at its entry point (via
    `ret_from_fork` and IRET if user mode).
-   The old task's state is saved into its `thread_struct` (ESP, EIP
    pointing after `switch_to`, etc.).
-   If it's not the first execution, the new stack already contains a
    saved context (from the previous switch).

The flow loops: the next interrupt may trigger another `schedule()`. To
verify, use `objdump -d vmlinux | grep switch_to` on your kernel. Errors
often come from incorrect artificial frame alignment (32-bit stacks must
be aligned on 4 bytes).
