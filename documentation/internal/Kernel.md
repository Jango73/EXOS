# Kernel documentation

## Architecture

To be completed.

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

