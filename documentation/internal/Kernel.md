# Kernel documentation

## Architecture

To be completed.

## Startup sequence

The DOS loader jumps to the kernel stub at `StartAbsolute` which only redirects
execution to `Start`:

```asm
global StartAbsolute

StartAbsolute :
    jmp     Start
```

`Start` runs in 16‑bit real mode. It saves the loader stack, adjusts the segment
registers, obtains the console cursor position and disables interrupts. The
routine then loads a temporary IDT and GDT, enables the A20 line and programs
the PIC for protected mode. Finally the protection bit in CR0 is set and control
is transferred to `Start32`:

```asm
mov     eax, CR0_PROTECTEDMODE
mov     cr0, eax

jmp     Next
Next :
    jmp     far dword [Start32_Entry - StartAbsolute]
```

`Start32` executes in 32‑bit mode without paging. It sets up the segment
registers, determines the available RAM with `GetMemorySize` and clears the
system memory area. The stub then copies the GDT to its final location, creates
page tables via `SetupPaging`, loads the page directory and copies the kernel to
its high memory address. Paging is enabled and the final GDT is loaded before
jumping to `ProtectedModeEntry`:

```asm
mov     eax, cr0
or      eax, CR0_PAGING
mov     cr0, eax

mov     eax, ProtectedModeEntry
jmp     eax
```

`ProtectedModeEntry` installs the kernel data selectors in all segment
registers, stores the stub base address and builds the kernel stack. After
clearing the general registers the code jumps to the C entry point:

```asm
mov     [StubAddress], ebp
mov     esp, KernelStack
add     esp, STK_SIZE
jmp     KernelMain
```

`KernelMain` is defined in `Main.c` and immediately calls `InitializeKernel` to
perform all C‑level initialization tasks.

## Modules and functions

### Clock.c

Implements the system timer and time utilities.

#### Functions in Clock.c

- InitializeClock: Configures the timer chip and enables IRQ 0.
- GetSystemTime: Returns the raw system time in milliseconds.
- MilliSecondsToHMS: Converts milliseconds to a HH:MM:SS string.
- ClockHandler: Interrupt handler that updates the system time and triggers the scheduler.
- ReadCMOS: Reads a byte from CMOS memory.
- WriteCMOS: Writes a byte to CMOS memory.
- GetLocalTime: Reads date and time from CMOS.

### Console.c

Provides text console output and input features.

#### Functions in Console.c

- SetConsoleCursorPosition: Moves the hardware cursor to a given position.
- SetConsoleCharacter: Writes a character with attributes at the current cursor location.
- ScrollConsole: Scrolls the console one line up when needed.
- ClearConsole: Clears the entire console screen.
- ConsolePrintChar: Displays a character and handles newlines and tabs.
- ConsoleBackSpace: Deletes the character before the cursor.
- SetConsoleBackColor: Changes the background color.
- SetConsoleForeColor: Changes the foreground color.
- ConsolePrint: Displays a string on the console.
- SkipAToI: Converts numeric characters to an integer (internal).
- VarKernelPrintNumber: Formats and prints a number.
- VarKernelPrint: Core printf-style formatter used by KernelPrint.
- KernelPrint: Prints formatted text to the console.
- ConsoleGetString: Reads a line of text from the console.
- ConsoleInitialize: Initializes the console state.

### Crypt.c

Provides basic password handling functions.

#### Functions in Crypt.c

- MakePassword: Generates an encrypted password from plain text.
- CheckPassword: Verifies a plain text password against an encrypted one.

### Desktop.c

Manages the graphical desktop and windows.

#### Functions in Desktop.c

- ResetGraphicsContext: Initializes a graphics context to default values.
- SortDesktops_Order: Sorts desktops based on their order field.
- SortWindows_Order: Sorts windows according to their order on screen.
- CreateDesktop: Allocates and sets up a new desktop structure.
- ShowDesktop: Displays a desktop using the graphics driver.
- NewWindow: Creates an empty window structure.
- FindWindow: Searches recursively for a child window.
- CreateWindow: Builds a window and attaches it to the hierarchy.
- GetWindowDesktop: Returns the desktop associated with a window.
- BroadCastMessage: Sends a message to a window and its children.
- RectInRect: Tests if two rectangles intersect.
- WindowRectToScreenRect: Converts window-relative coordinates to screen coordinates.
- ScreenRectToWindowRect: Converts screen coordinates to window-relative values.
- InvalidateWindowRect: Marks a window region as needing redraw.
- BringWindowToFront: Moves a window ahead of its siblings.
- ShowWindow: Hides or shows a window.
- GetWindowRect: Retrieves the window rectangle.
- MoveWindow: Moves a window on the screen.
- SizeWindow: Resizes a window.
- GetWindowParent: Returns the parent of a window.
- SetWindowProp: Stores a custom property in a window.
- GetWindowProp: Retrieves a custom property from a window.
- GetWindowGC: Provides a graphics context for drawing.
- ReleaseWindowGC: Releases a graphics context obtained earlier.
- BeginWindowDraw: Locks a window for drawing.
- EndWindowDraw: Unlocks a window after drawing.
- GetSystemBrush: Returns a system-defined brush handle.
- GetSystemPen: Returns a system-defined pen handle.
- SelectBrush: Selects a brush into a graphics context.
- SelectPen: Selects a pen into a graphics context.
- CreateBrush: Allocates a new brush object.
- CreatePen: Allocates a new pen object.
- SetPixel: Writes a single pixel at a position.
- GetPixel: Reads a pixel value from a position.
- Line: Draws a line using the current context settings.
- Rectangle: Draws a filled rectangle.
- WindowHitTest: Finds the top window at a screen position.
- DefWindowFunc: Default window message procedure.
- DesktopWindowFunc: Core desktop message handler.

### DrvCall.c

Handles user mode calls to drivers.

#### Functions in DrvCall.c

- DriverFunc: Placeholder function used in the driver call table.
- DriverCallHandler: Dispatches a call to the appropriate driver function.

### Edit.c

Implements a simple text editor for the shell.

#### Functions in Edit.c

- NewEditLine: Allocates a line structure with a given size.
- DeleteEditLine: Releases resources for a line.
- EditLineDestructor: Helper used when deleting line lists.
- NewEditFile: Creates a new editable file context.
- DeleteEditFile: Destroys an editable file and its lines.
- EditFileDestructor: Helper used when removing files from a list.
- NewEditContext: Creates the top-level editor context.
- DeleteEditContext: Releases the editor context.
- CheckPositions: Adjusts viewport offsets according to cursor position.
- DrawText: Renders the text buffer onto the console.
- CheckLineSize: Grows a line buffer when needed.
- FillToCursor: Inserts spaces until the cursor column is valid.
- GetCurrentLine: Returns the line at the current cursor row.
- AddCharacter: Inserts a character at the cursor position.
- DeleteCharacter: Removes a character before or after the cursor.
- AddLine: Inserts a new line in the file.
- GotoEndOfLine: Moves the cursor to the end of the current line.
- GotoStartOfLine: Moves the cursor to the start of the current line.
- Loop: Main event loop of the text editor.
- OpenTextFile: Loads a text file into the editor context.
- Edit: Entry point used by the shell to invoke the editor.

### FAT16.c

Driver implementation for the FAT16 file system.

#### Functions in FAT16.c

- FAT16Commands: Driver entry point handling file system requests.
- NewFAT16FileSystem: Allocates a FAT16 file system object.
- NewFATFile: Allocates a FAT16 file structure.
- MountPartition_FAT16: Mounts a FAT16 partition on a disk.
- ReadCluster: Reads a cluster from disk into memory.
- WriteCluster: Writes a cluster back to disk.
- GetNextClusterInChain: Returns the next cluster in a file chain.
- DecodeFileName: Converts directory entry names to strings.
- TranslateFileInfo: Fills a file object from a directory entry.
- LocateFile: Locates a file by path on the file system.
- Initialize: Initializes the FAT16 driver.
- OpenFile: Opens a file given search information.
- OpenNext: Opens the next file in a directory iteration.
- CloseFile: Closes a previously opened file.
- ReadFile: Reads data from an open file.


### FAT32.c

Driver implementation for the FAT32 file system.

#### Functions in FAT32.c

- FAT32Commands: Handles all FAT32 driver operations.
- NewFATFileSystem: Allocates a FAT32 file system object.
- NewFATFile: Allocates a FAT32 file structure.
- MountPartition_FAT32: Mounts a FAT32 partition on disk.
- GetNameChecksum: Computes the checksum for short filenames.
- ReadCluster: Reads a cluster from disk.
- WriteCluster: Writes a cluster back to disk.
- GetNextClusterInChain: Retrieves the next cluster in a file chain.
- FindFreeCluster: Searches the FAT for a free cluster.
- FindFreeFATEntry: Locates a free directory entry slot.
- SetDirEntry: Fills a directory entry buffer.
- CreateDirEntry: Creates a directory entry on disk.
- ChainNewCluster: Links a new cluster to a file.
- DecodeFileName: Converts directory entries to file names.
- LocateFile: Finds a file by path.
- TranslateFileInfo: Copies directory entry data to a file object.
- Initialize: Initializes the FAT32 driver.
- CreateFolder: Creates a new folder on the file system.
- DeleteFolder: Removes a folder.
- RenameFolder: Renames a folder.
- OpenFile: Opens a file.
- OpenNext: Gets the next file in a directory listing.
- CloseFile: Closes an open file.
- ReadFile: Reads file data.
- WriteFile: Writes data to a file.
- CreatePartition: Creates a FAT32 partition.

### Fault.c

Exception and fault handlers for the kernel.

#### Functions in Fault.c

- PrintFaultDetails: Displays process and register information.
- Die: Terminates the current task and halts.
- DefaultHandler: Generic handler for unknown interrupts.
- DivideErrorHandler: Handles divide by zero faults.
- DebugExceptionHandler: Handles debug exceptions.
- NMIHandler: Handles non-maskable interrupts.
- BreakPointHandler: Handles breakpoint exceptions.
- OverflowHandler: Handles arithmetic overflow faults.
- BoundRangeHandler: Handles bound range exceeded faults.
- InvalidOpcodeHandler: Handles invalid opcode faults.
- DeviceNotAvailHandler: Handles device-not-available exceptions.
- DoubleFaultHandler: Handles double fault exceptions.
- MathOverflowHandler: Handles math overflow faults.
- InvalidTSSHandler: Handles invalid TSS faults.
- SegmentFaultHandler: Handles segment faults.
- StackFaultHandler: Handles stack faults.
- GeneralProtectionHandler: Handles general protection faults.
- PageFaultHandler: Handles page fault exceptions.
- AlignmentCheckHandler: Handles alignment check faults.

### File.c

High level functions to access files through drivers.

#### Functions in File.c

- OpenFile: Opens a file using the registered file systems.
- CloseFile: Closes an open file and releases its resources.
- ReadFile: Reads bytes from a file.
- WriteFile: Writes bytes to a file.
- GetFileSize: Returns the size of a file.

### FileSys.c

Handles mounting of disk partitions and path manipulation.

#### Functions in FileSys.c

- GetNumFileSystems: Returns the number of available file systems.
- GetDefaultFileSystemName: Builds a default name for new file systems.
- MountPartition_Extended: Mounts an extended partition.
- MountDiskPartitions: Scans a disk and mounts each partition.
- DecompPath: Splits a path into its individual components.

### HD.c

Implements the standard hard disk driver and caching logic.

#### Functions in HD.c

- NewStdHardDisk: Creates a disk structure and sets defaults.
- SectorToBlockParams: Translates sector numbers to CHS values.
- WaitNotBusy: Waits until the controller is ready for commands.
- HardDiskInitialize: Detects drives and prepares buffers.
- ControllerBusy: Checks if the controller is busy.
- IsStatusOk: Validates status bits from the controller.
- IsControllerReady: Waits for a drive to be ready.
- ResetController: Issues a controller reset sequence.
- DriveOut: Performs a low-level read or write.
- FindSectorInBuffers: Searches the cache for a sector.
- GetEmptyBuffer: Selects a buffer slot for new data.
- Read: Reads sectors through the cache.
- Write: Writes sectors to disk.
- GetInfo: Fills a structure with disk information.
- SetAccess: Changes access permissions on the disk.
- HardDriveHandler: Interrupt handler for disk operations.
- StdHardDiskCommands: Dispatches driver requests.

### Heap.c

Kernel heap memory manager used by processes.

#### Functions in Heap.c

- HeapAlloc_HBHS: Allocates memory from a heap block.
- HeapFree_HBHS: Releases a block from the heap.
- HeapAlloc_P: Allocates memory in the context of a process.
- HeapFree_P: Frees memory in the context of a process.
- HeapAlloc: Allocates memory from the current process.
- HeapFree: Frees memory from the current process.

### Kernel.c

Core initialization and debugging utilities for the kernel.

#### Functions in Kernel.c

- KernelMemAlloc: Allocates memory from the kernel heap.
- KernelMemFree: Returns memory to the kernel heap.
- SetGateDescriptorOffset: Writes an offset into an IDT entry.
- InitializeInterrupts: Sets up the interrupt descriptor table.
- GetSegmentInfo: Retrieves segment descriptor data.
- SegmentInfoToString: Formats segment info as text.
- DumpGlobalDescriptorTable: Prints all GDT entries.
- DumpRegisters: Displays the CPU register state.
- GetCPUInformation: Reads processor name and features.
- ClockTask: Periodic task that updates the clock and mouse.
- DumpSystemInformation: Logs CPU and memory information.
- InitializePhysicalPageBitmap: Marks kernel pages as used.
- InitializeFileSystems: Mounts all detected file systems.
- GetPhysicalMemoryUsed: Returns the number of used bytes.
- InitializeKernel: Performs global kernel initialization.

### Keyboard.c

Implements the PC keyboard driver and key buffering.

#### Functions in Keyboard.c

- KeyboardWait: Waits for the controller input buffer to clear.
- KeyboardACK: Checks if the keyboard acknowledged a command.
- SendKeyboardCommand: Sends a command and data byte to the keyboard.
- ScanCodeToKeyCode: Converts hardware scan codes to key codes.
- ScanCodeToKeyCode_E0: Handles extended scan code prefix E0.
- ScanCodeToKeyCode_E1: Handles extended scan code prefix E1.
- SendKeyCodeToBuffer: Stores a translated key code in the buffer.
- UpdateKeyboardLEDs: Updates the state of the keyboard LEDs.
- GetKeyboardLEDs: Returns the current LED status.
- SetKeyboardLEDs: Changes LED states.
- HandleScanCode: Processes a received scan code.
- PeekChar: Checks if a character is available.
- GetChar: Reads a character from the buffer.
- GetKeyCode: Reads a full key code from the buffer.
- WaitKey: Waits for a key press.
- KeyboardHandler: Interrupt handler that reads scan codes.
- KeyboardInitialize: Initializes keyboard structures and IRQ.
- StdKeyboardCommands: Driver entry function.

### List.c

Provides a generic linked list container and sorting routine.

#### Functions in List.c

- QuickSort: Sorts an array of items using quicksort.
- NewList: Allocates a new list object.
- DeleteList: Releases all nodes and the list structure.
- ListGetSize: Returns the number of items.
- ListAddItem: Appends an item at the end.
- ListAddBefore: Inserts an item before another item.
- ListAddAfter: Inserts an item after another item.
- ListAddHead: Inserts an item at the head.
- ListAddTail: Inserts an item at the tail.
- ListRemove: Removes a node without deleting it.
- ListErase: Removes and frees a node.
- ListEraseLast: Erases the last item.
- ListEraseItem: Erases a specific item.
- ListReset: Clears the list.
- ListGetItem: Gets the item at a specified index.
- ListGetItemIndex: Returns the index of an item.
- ListMergeList: Appends another list and deletes it.
- ListSort: Sorts the list using a comparison function.

### Log.c

Minimal logging utility for kernel messages.

#### Functions in Log.c

- KernelLogText: Prints a log message based on severity.

### Main.c

Kernel entry point after transitioning to protected mode.

#### Functions in Main.c

- DebugPutChar: Writes a character directly to video memory.
- KernelIdle: Idle loop executed when no tasks run.
- KernelMain: Initializes the kernel and enters idle loop.

### Memedit.c

Interactive memory viewer used for debugging.

#### Functions in Memedit.c

- PrintMemoryLine: Displays a single line of memory values.
- PrintMemory: Dumps a series of memory lines.
- PrintMemoryPage: Shows a page of memory on the console.
- MemEdit: Lets the user scroll through memory addresses.
### Process.c

Manages executable loading, process creation and heap setup.

#### Functions in Process.c

- InitializeKernelHeap: Sets up the kernel heap area.
- GetExecutableInfo_EXOS: Retrieves information from an EXOS executable.
- LoadExecutable_EXOS: Loads an executable into memory.
- NewProcess: Allocates and initializes a process structure.
- CreateProcess: Loads a program and creates its initial task.
- GetProcessHeap: Returns the base address of a process heap.
- DumpProcess: Prints process details for debugging.
- InitSecurity: Initializes a security descriptor.

### RAMDisk.c

Driver for an in-memory disk used mainly for testing.

#### Functions in RAMDisk.c

- NewRAMDisk: Allocates a RAM disk structure.
- CreateFATDirEntry: Builds a FAT directory entry.
- FormatRAMDisk_FAT32: Creates a FAT32 layout on the disk.
- RAMDiskInitialize: Allocates and sets up the RAM disk.
- Read: Reads sectors from the RAM disk.
- Write: Writes sectors to the RAM disk.
- GetInfo: Returns information about the disk.
- SetAccess: Changes disk access permissions.
- RAMDiskCommands: Dispatches functions for the RAM disk driver.

### Schedule.c

Implements task scheduling and queue management.

#### Functions in Schedule.c

- UpdateScheduler: Recomputes time slices for tasks.
- AddTaskToQueue: Inserts a new task in the scheduler queue.
- RemoveTaskFromQueue: Removes a task from the scheduler queue.
- RotateQueue: Moves to the next task in the round-robin list.
- Scheduler: Chooses the next task to run.
- GetCurrentProcess: Returns the process owning the current task.
- GetCurrentTask: Returns the currently running task.
- FreezeScheduler: Increments scheduler freeze counter.
- UnfreezeScheduler: Decrements scheduler freeze counter.

### Segment.c

Helper functions to initialize GDT and TSS descriptors.

#### Functions in Segment.c

- InitSegmentDescriptor: Initializes a segment descriptor.
- SetSegmentDescriptorBase: Sets the base address of a segment.
- SetSegmentDescriptorLimit: Sets the limit of a segment.
- SetTSSDescriptorBase: Sets the base of a TSS descriptor.
- SetTSSDescriptorLimit: Sets the size of a TSS descriptor.

### Mutex.c

A mutex providing mutual exclusion.

#### Functions in Mutex.c

- InitMutex: Resets a mutex object.
- NewMutex: Allocates an unlinked mutex.
- CreateMutex: Allocates and registers a mutex.
- DeleteMutex: Removes a mutex from the kernel list.
- LockMutex: Acquires the mutex for the current task.
- UnlockMutex: Releases a previously acquired mutex.

### SerMouse.c

Serial mouse driver using a COM port.

#### Functions in SerMouse.c

- SendBreak: Sends a break signal on the serial line.
- Delay: Waits a short time for hardware operations.
- WaitMouseData: Polls the UART for incoming mouse data.
- MouseInitialize: Detects and initializes the mouse hardware.
- GetDeltaX: Returns the accumulated X movement.
- GetDeltaY: Returns the accumulated Y movement.
- GetButtons: Gets the state of mouse buttons.
- DrawMouseCursor: Draws a simple cross cursor on screen.
- MouseHandler_Microsoft: Reads Microsoft protocol packets.
- MouseHandler_MouseSystems: Reads Mouse Systems packets.
- MouseHandler: Dispatches to the correct protocol handler.
- SerialMouseCommands: Entry point for driver requests.

### Shell.c

Simple command interpreter for user interaction.

#### Functions in Shell.c

- InitShellContext: Initializes buffers and current path.
- DeinitShellContext: Releases shell buffers.
- RotateBuffers: Maintains the command history.
- ShowPrompt: Prints the current prompt.
- ParseNextComponent: Extracts the next token from input.
- GetCurrentFileSystem: Finds the active file system object.
- QualifyFileName: Builds a fully qualified file name.
- ChangeFolder: Changes the current directory.
- MakeFolder: Creates a new folder.
- ListFile: Displays file information in a directory.
- CMD_commands: Lists available shell commands.
- CMD_cls: Clears the console output.
- CMD_dir: Shows files in the current directory.
- CMD_cd: Changes the current directory.
- CMD_md: Creates a folder using MakeFolder.
- CMD_run: Launches an executable file.
- CMD_exit: Exits the shell loop.
- CMD_sysinfo: Displays system information.
- CMD_killtask: Terminates a task by index.
- CMD_showprocess: Dumps process information.
- CMD_showtask: Dumps task information.
- CMD_memedit: Invokes the memory editor.
- CMD_cat: Prints the contents of a file.
- CMD_copy: Copies one file to another.
- CMD_edit: Opens the text editor.
- CMD_hd: Lists hard disk information.
- CMD_filesystem: Lists installed file systems.
- ParseCommand: Parses a command line and dispatches it.
- Shell: Main entry that runs the shell loop.

### String.c

String manipulation helpers used across the kernel.

#### Functions in String.c

- IsAlpha: Checks if a character is alphabetic.
- IsNumeric: Tests if a character is numeric.
- IsAlphaNumeric: Tests if a character is alphanumeric.
- CharToLower: Converts a character to lowercase.
- CharToUpper: Converts a character to uppercase.
- StringLength: Returns the length of a string.
- StringCopy: Copies a string to a destination buffer.
- StringCopyNum: Copies a fixed number of characters.
- StringConcat: Appends one string to another.
- StringCompare: Compares two strings case sensitively.
- StringCompareNC: Compares two strings ignoring case.
- StringToLower: Converts a string to lowercase in-place.
- StringToUpper: Converts a string to uppercase in-place.
- StringFindChar: Finds the first occurrence of a character.
- StringFindCharR: Finds the last occurrence of a character.
- StringInvert: Reverses a string in-place.
- U32ToString: Converts an integer to a string.
- U32ToHexString: Converts an integer to hexadecimal text.
- HexStringToU32: Parses a hexadecimal number.
- StringToI32: Converts a string to a signed integer.
- StringToU32: Converts a string to an unsigned integer.
- NumberToString: Formats a number according to flags.

### SYSCall.c

System call handler dispatching user mode requests.

#### Functions in SYSCall.c

- SysCall_GetVersion: Returns the kernel version.
- SysCall_GetSystemInfo: Fills a SYSTEMINFO structure.
- SysCall_GetLastError: Retrieves the last error code.
- SysCall_SetLastError: Sets the last error code.
- SysCall_GetSystemTime: Gets the tick count in milliseconds.
- SysCall_GetLocalTime: Retrieves the current date and time.
- SysCall_SetLocalTime: Sets the current date and time.
- SysCall_DeleteObject: Closes an object handle.
- SysCall_CreateProcess: Creates a new process.
- SysCall_KillProcess: Terminates a process.
- SysCall_CreateTask: Creates a new task in the current process.
- SysCall_KillTask: Kills a task given its handle.
- SysCall_SuspendTask: Suspends a task.
- SysCall_ResumeTask: Resumes a suspended task.
- SysCall_Sleep: Sleeps for a number of milliseconds.
- SysCall_PostMessage: Posts a window message.
- SysCall_SendMessage: Sends a window message synchronously.
- SysCall_PeekMessage: Checks if a message is pending.
- SysCall_GetMessage: Waits for a message from the queue.
- SysCall_DispatchMessage: Dispatches a message to a window.
- SysCall_CreateSemaphore: Creates a new mutex.
- SysCall_DeleteSemaphore: Deletes a mutex object.
- SysCall_LockSemaphore: Locks a mutex with timeout.
- SysCall_UnlockSemaphore: Unlocks a mutex.
- SysCall_VirtualAlloc: Allocates virtual memory pages.
- SysCall_VirtualFree: Frees virtual memory pages.
- SysCall_GetProcessHeap: Returns the heap of a process.
- SysCall_HeapAlloc: Allocates memory from the heap.
- SysCall_HeapFree: Releases heap memory.
- SysCall_EnumVolumes: Enumerates mounted volumes.
- SysCall_GetVolumeInfo: Retrieves volume information.
- SysCall_OpenFile: Opens a file handle.
- SysCall_ReadFile: Reads data from a file.
- SysCall_WriteFile: Writes data to a file.
- SysCall_GetFileSize: Returns the size of a file.
- SysCall_GetFilePointer: Gets the current file offset.
- SysCall_SetFilePointer: Sets the current file offset.
- SysCall_ConsolePeekKey: Checks if a key is ready.
- SysCall_ConsoleGetKey: Reads a key code.
- SysCall_ConsolePrint: Prints text to the console.
- SysCall_ConsoleGetString: Reads a line from the console.
- SysCall_ConsoleGotoXY: Moves the console cursor.
- SysCall_CreateDesktop: Creates a desktop object.
- SysCall_ShowDesktop: Displays a desktop on screen.
- SysCall_GetDesktopWindow: Gets the root window of a desktop.
- SysCall_CreateWindow: Creates a window object.
- SysCall_ShowWindow: Displays a window.
- SysCall_HideWindow: Hides a window from view.
- SysCall_MoveWindow: Moves a window on screen.
- SysCall_SizeWindow: Resizes a window.
- SysCall_SetWindowFunc: Sets the callback for a window.
- SysCall_GetWindowFunc: Retrieves the window callback.
- SysCall_SetWindowStyle: Changes the window style bits.
- SysCall_GetWindowStyle: Reads the window style bits.
- SysCall_SetWindowProp: Stores a custom property.
- SysCall_GetWindowProp: Retrieves a custom property.
- SysCall_GetWindowRect: Returns window position and size.
- SysCall_InvalidateWindowRect: Marks a region dirty.
- SysCall_GetWindowGC: Gets a graphics context for drawing.
- SysCall_ReleaseWindowGC: Releases a graphics context.
- SysCall_EnumWindows: Enumerates child windows.
- SysCall_DefWindowFunc: Default procedure for windows.
- SysCall_GetSystemBrush: Gets a stock brush handle.
- SysCall_GetSystemPen: Gets a stock pen handle.
- SysCall_CreateBrush: Creates a brush resource.
- SysCall_CreatePen: Creates a pen resource.
- SysCall_SelectBrush: Selects a brush into a GC.
- SysCall_SelectPen: Selects a pen into a GC.
- SysCall_SetPixel: Draws a pixel using the graphics driver.
- SysCall_GetPixel: Reads a pixel from a position.
- SysCall_Line: Draws a line primitive.
- SysCall_Rectangle: Draws a filled rectangle.
- SysCall_GetMousePos: Retrieves mouse coordinates.
- SysCall_SetMousePos: Moves the mouse cursor.
- SysCall_GetMouseButtons: Gets mouse button state.
- SysCall_ShowMouse: Shows the mouse cursor.
- SysCall_HideMouse: Hides the mouse cursor.
- SysCall_ClipMouse: Sets the mouse clip region.
- SysCall_CaptureMouse: Captures mouse input.
- SysCall_ReleaseMouse: Releases the mouse capture.
- SystemCallHandler: Dispatches a system call number.

### SystemFS.c

Basic virtual file system for system resources.

#### Functions in SystemFS.c

- NewSystemFileRoot: Creates the root system file entry.
- NewSystemFSFileSystem: Allocates the file system object.
- NewSysFSFile: Creates an in-memory file object.
- MountSystemFS: Registers the system file system.
- Initialize: Initializes the driver instance.
- OpenFile: Opens a system file from its path.
- OpenNext: Enumerates the next file in a directory.
- CloseFile: Closes an open system file.
- ReadFile: Reads data from a system file.
- WriteFile: Writes data to a system file.
- SystemFSCommands: Driver entry to handle requests.

### Task.c

Manages task creation, messaging and scheduling features.

#### Functions in Task.c

- NewMessage: Allocates a new message structure.
- DeleteMessage: Releases a message structure.
- MessageDestructor: Helper used to delete messages from lists.
- NewTask: Creates an empty task object.
- DeleteTask: Frees a task and its stacks.
- InitKernelTask: Initializes the kernel task and TSS.
- CreateTask: Builds a task from information in a process.
- KillTask: Terminates a running task.
- SetTaskPriority: Changes task priority and returns the previous value.
- Sleep: Delays execution of the current task.
- GetTaskStatus: Returns the status flags of a task.
- SetTaskStatus: Updates the task status flags.
- AddTaskMessage: Queues a message for a task.
- PostMessage: Posts a message without waiting for completion.
- SendMessage: Sends a message and waits until handled.
- WaitForMessage: Blocks the caller until a message is present.
- GetMessage: Retrieves the next message for a task.
- DispatchMessage: Routes a message to its destination window.
- DumpTask: Prints debugging information for a task.

### Text.c

Kernel text constants used for interface messages.

#### Functions in Text.c

- Text_NewLine: "\n" string constant.
- Text_Space: Single space string constant.
- Text_Colon: ":" string constant.
- Text_0: "0" string constant.
- Text_KB: "KB" string constant.
- Text_EnablingIRQs: Message displayed when IRQs are enabled.
- Text_Exit: "Exit" string constant.
- Text_Registers: Prefix for register dumps.
- Text_Image: "Image :" label string.
- Text_Clk: "Clk" string constant.

### VESA.c

Driver for VESA compatible graphics hardware.

#### Functions in VESA.c

- VESAInitialize: Initializes the VESA video driver.
- VESAUninitialize: Releases VESA resources.
- SetVideoMode: Switches to a specific video mode.
- SetVESABank: Selects the active memory bank.
- SetClip: Defines the drawing clip rectangle.
- SetPixel8: Writes an 8-bit pixel to video memory.
- SetPixel16: Writes a 16-bit pixel to video memory.
- SetPixel24: Writes a 24-bit pixel to video memory.
- GetPixel8: Reads an 8-bit pixel from video memory.
- GetPixel16: Reads a 16-bit pixel from video memory.
- GetPixel24: Reads a 24-bit pixel from video memory.
- Line8: Draws a line in 8-bit modes.
- Line16: Draws a line in 16-bit modes.
- Line24: Draws a line in 24-bit modes.
- Rect8: Draws a filled rectangle in 8-bit modes.
- Rect16: Draws a filled rectangle in 16-bit modes.
- Rect24: Draws a filled rectangle in 24-bit modes.
- VESA_CreateBrush: Creates a brush object for drawing.
- VESA_CreatePen: Creates a pen object for drawing.
- VESA_SetPixel: Generic set pixel operation.
- VESA_GetPixel: Generic get pixel operation.
- VESA_Line: Generic line drawing routine.
- VESA_Rectangle: Generic rectangle drawing routine.
- VESACommands: Entry point used by the driver manager.

### VGA.c

Low level routines for standard VGA mode programming.

#### Functions in VGA.c

- VGAIODelay: Short delay used when programming the VGA registers.
- SendModeRegs: Loads a register set to configure video mode.
- TestVGA: Simple routine that programs the first VGA mode.

### VMM.c

Kernel virtual memory manager for page allocation and mapping.

#### Functions in VMM.c

- InitializeVirtualMemoryManager: Detects memory size and page count.
- SetPhysicalPageMark: Marks a physical page as used or free.
- GetPhysicalPageMark: Returns the allocation state of a page.
- AllocPhysicalPage: Reserves a free physical page.
- GetDirectoryEntry: Returns the page directory entry index for an address.
- GetTableEntry: Returns the page table entry index for an address.
- AllocPageDirectory: Allocates a page directory for a process.
- AllocPageTable: Creates a new page table if required.
- IsRegionFree: Tests if a virtual memory region is unused.
- FindFreeRegion: Searches for a free area of virtual memory.
- FreeEmptyPageTables: Releases unused page tables.
- VirtualAlloc: Maps a range of virtual memory with given flags.
- VirtualFree: Unmaps a range of virtual memory pages.

### XFS.c

EXOS file system driver for the native XFS format.

#### Functions in XFS.c

- NewXFSFileSystem: Allocates an XFS file system structure.
- NewXFSFile: Creates an XFS file object.
- MountPartition_XFS: Mounts an XFS partition from disk.
- ReadCluster: Reads a cluster from the disk into memory.
- WriteCluster: Writes a cluster back to disk.
- LocateFile: Finds a file entry given its path.
- WriteSectors: Writes raw sectors to a disk device.
- CreatePartition: Formats a new XFS partition.
- TranslateFileInfo: Copies XFS directory info to a file object.
- Initialize: Loads the driver and returns success.
- OpenFile: Opens a file using XFS search logic.
- OpenNext: Continues directory enumeration.
- CloseFile: Closes an open XFS file.
- XFSCommands: Dispatcher for driver functions.
