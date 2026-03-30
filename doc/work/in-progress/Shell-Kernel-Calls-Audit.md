# Shell Kernel Calls Audit

## Scope

Static audit of direct symbol calls from:

- `kernel/source/shell/Shell-Commands.c`
- `kernel/source/shell/Shell-Commands-Core.c`
- `kernel/source/shell/Shell-Commands-Graphics.c`
- `kernel/source/shell/Shell-Commands-Package.c`
- `kernel/source/shell/Shell-Commands-Storage.c`
- `kernel/source/shell/Shell-Commands-System.c`
- `kernel/source/shell/Shell-Commands-Users.c`
- `kernel/source/shell/Shell-Execution.c`
- `kernel/source/shell/Shell-Main.c`

Method:

- only direct call sites were counted
- shell-local function definitions were excluded
- obvious C keywords and common macros were excluded
- only symbols that have a definition outside the shell were retained
- this is a source-level symbol audit, not a runtime trace

## Global Count

- unique non-shell symbols called from the shell: `170`
- total direct call sites across shell sources: `861`

## Highest Counts

| Function | Count |
|---|---:|
| `ConsolePrint` | 325 |
| `StringLength` | 58 |
| `StringCopy` | 53 |
| `StringCompareNC` | 43 |
| `StringConcat` | 18 |
| `MemorySet` | 13 |
| `PackageManifestRelease` | 13 |
| `SizeFormatBytesText` | 11 |
| `ScriptRegisterHostSymbol` | 8 |
| `StringPrintFormat` | 8 |
| `StringToU32` | 8 |
| `CommandLineEditorReadLine` | 7 |
| `GetCurrentSession` | 7 |
| `Sleep` | 7 |
| `GetDriverList` | 6 |
| `PackageFSUnmount` | 6 |
| `StringCompare` | 6 |
| `FindUserAccountByID` | 5 |
| `GetCurrentProcess` | 5 |
| `GetSystemFS` | 5 |
| `GetSystemPen` | 5 |
| `GraphicsSelectorGetDriver` | 5 |
| `ProcessControlIsInterruptRequested` | 5 |
| `SelectPen` | 5 |

## Sensitive Direct Kernel Coupling

These are the calls that matter most for the shell/kernel boundary discussion:

- `GetTaskList`: `1`
- `ListGetItem`: `1`
- `KillTaskForCurrentProcess`: `1`
- `GetProcessList`: `1`
- `GetCurrentProcess`: `5`
- `GetCurrentTask`: `4`
- `CreateProcess`: `1`
- `Spawn`: `1`
- `Edit`: `2`
- `CreateDesktop`: `2`
- `DeleteDesktop`: `3`
- `CreateWindow`: `1`
- `DisplaySwitchToDesktop`: `2`
- `DisplaySwitchToConsole`: `1`
- `GetProcessMemoryRegionList`: `1`
- `GetNetworkDeviceList`: `1`
- `GetDriverList`: `6`
- `GetDiskList`: `2`
- `GetFileSystemList`: `1`
- `GetUnusedFileSystemList`: `1`
- `GetUsbStorageList`: `1`
- `GetUserAccountList`: `3`

## Per File Summary

| File | Direct non-shell call sites |
|---|---:|
| `kernel/source/shell/Shell-Commands-Core.c` | 173 |
| `kernel/source/shell/Shell-Commands-Graphics.c` | 152 |
| `kernel/source/shell/Shell-Commands-System.c` | 139 |
| `kernel/source/shell/Shell-Commands-Package.c` | 134 |
| `kernel/source/shell/Shell-Commands-Users.c` | 89 |
| `kernel/source/shell/Shell-Commands-Storage.c` | 84 |
| `kernel/source/shell/Shell-Main.c` | 66 |
| `kernel/source/shell/Shell-Execution.c` | 24 |
| `kernel/source/shell/Shell-Commands.c` | 0 |

## Full List By Total Count

| Function | Count |
|---|---:|
| `ConsolePrint` | 325 |
| `StringLength` | 58 |
| `StringCopy` | 53 |
| `StringCompareNC` | 43 |
| `StringConcat` | 18 |
| `MemorySet` | 13 |
| `PackageManifestRelease` | 13 |
| `SizeFormatBytesText` | 11 |
| `ScriptRegisterHostSymbol` | 8 |
| `StringPrintFormat` | 8 |
| `StringToU32` | 8 |
| `CommandLineEditorReadLine` | 7 |
| `GetCurrentSession` | 7 |
| `Sleep` | 7 |
| `GetDriverList` | 6 |
| `PackageFSUnmount` | 6 |
| `StringCompare` | 6 |
| `FindUserAccountByID` | 5 |
| `GetCurrentProcess` | 5 |
| `GetSystemFS` | 5 |
| `GetSystemPen` | 5 |
| `GraphicsSelectorGetDriver` | 5 |
| `ProcessControlIsInterruptRequested` | 5 |
| `SelectPen` | 5 |
| `AllocatorFree` | 4 |
| `ConsoleGetString` | 4 |
| `DestroyUserSession` | 4 |
| `FileReadAll` | 4 |
| `PackageManifestStatusToString` | 4 |
| `PackageNamespaceUnbindCurrentProcessPackageView` | 4 |
| `StringEmpty` | 4 |
| `ActivateTheme` | 3 |
| `AllocatorAlloc` | 3 |
| `CommandLineEditorSetIdleCallback` | 3 |
| `DeleteDesktop` | 3 |
| `GetUserAccountList` | 3 |
| `IsUserSessionLocked` | 3 |
| `KernelEnumGetProvider` | 3 |
| `KernelEnumNext` | 3 |
| `KernelPathBuildFile` | 3 |
| `PackageManifestParseFromPackageBuffer` | 3 |
| `SelectBrush` | 3 |
| `SetCurrentSession` | 3 |
| `StringFindChar` | 3 |
| `VerifyPassword` | 3 |
| `ClearConsole` | 2 |
| `ConsoleSetPagingEnabled` | 2 |
| `CreateDesktop` | 2 |
| `CreateUserSession` | 2 |
| `DesktopDrawText` | 2 |
| `DisplaySwitchToDesktop` | 2 |
| `DriverTypeToText` | 2 |
| `Edit` | 2 |
| `FileSystemGetPartitionFormatName` | 2 |
| `FileSystemGetPartitionTypeName` | 2 |
| `FileWriteAll` | 2 |
| `FindUserAccount` | 2 |
| `GetActiveDesktop` | 2 |
| `GetCurrentTask` | 2 |
| `GetDiskList` | 2 |
| `GetGraphicsDriver` | 2 |
| `GetKeyboardCode` | 2 |
| `GetSystemBrush` | 2 |
| `GraphicsSelectorGetActiveBackendName` | 2 |
| `KernelPathResolve` | 2 |
| `Line` | 2 |
| `LoadTheme` | 2 |
| `MemoryCopy` | 2 |
| `PackageFSMountFromBuffer` | 2 |
| `PackageNamespaceBindCurrentProcessPackageView` | 2 |
| `ProcessControlCheckpoint` | 2 |
| `ProcessControlConsumeInterrupt` | 2 |
| `Rectangle` | 2 |
| `SaveUserDatabase` | 2 |
| `ScriptExecute` | 2 |
| `ScriptGetErrorMessage` | 2 |
| `StringCopyNum` | 2 |
| `UnlockUserSession` | 2 |
| `XHCIEnumErrorToString` | 2 |
| `AllocatorInitProcess` | 1 |
| `BaseWindowFunc` | 1 |
| `BeginWindowDraw` | 1 |
| `CanAttemptSessionUnlock` | 1 |
| `CanAttemptUserAuthentication` | 1 |
| `ChangeUserPassword` | 1 |
| `CommandLineEditorDeinit` | 1 |
| `CommandLineEditorInitA` | 1 |
| `CommandLineEditorRemember` | 1 |
| `CommandLineEditorSetCompletionCallback` | 1 |
| `ConsoleCaptureActiveRegionSnapshot` | 1 |
| `ConsoleGetPagingEnabled` | 1 |
| `ConsolePrintChar` | 1 |
| `ConsoleReleaseActiveRegionSnapshot` | 1 |
| `ConsoleResetPaging` | 1 |
| `ConsoleRestoreActiveRegionSnapshot` | 1 |
| `CreateProcess` | 1 |
| `CreateUserAccount` | 1 |
| `CreateWindow` | 1 |
| `DeleteUserAccount` | 1 |
| `DesktopInternalRunStressDrag` | 1 |
| `DesktopMeasureText` | 1 |
| `Disassemble` | 1 |
| `DisplaySessionGetActiveFrontEnd` | 1 |
| `DisplaySessionSetConsoleGraphicsMode` | 1 |
| `DisplaySessionSetDesktopMode` | 1 |
| `DisplaySwitchToConsole` | 1 |
| `DriverDomainToText` | 1 |
| `EndWindowDraw` | 1 |
| `FileSystemGetPartitionSchemeName` | 1 |
| `FileSystemGetStorageUnit` | 1 |
| `FontGetDefaultFace` | 1 |
| `GetActiveThemeInfo` | 1 |
| `GetConfiguration` | 1 |
| `GetConfigurationValue` | 1 |
| `GetDoLogin` | 1 |
| `GetFileSystemGlobalInfo` | 1 |
| `GetFileSystemList` | 1 |
| `GetKeyboardDescriptor` | 1 |
| `GetKeyboardRootHandle` | 1 |
| `GetLocalTime` | 1 |
| `GetMouseDescriptor` | 1 |
| `GetMouseRootHandle` | 1 |
| `GetNetworkDeviceList` | 1 |
| `GetPCIDeviceList` | 1 |
| `GetProcessList` | 1 |
| `GetProcessMemoryRegionList` | 1 |
| `GetShowDesktop` | 1 |
| `GetTaskList` | 1 |
| `GetUnusedFileSystemList` | 1 |
| `GetUsbStorageList` | 1 |
| `GraphicsSelectorForceBackendByName` | 1 |
| `IsUserSessionTimedOut` | 1 |
| `KernelEnumPretty` | 1 |
| `KillTaskForCurrentProcess` | 1 |
| `ListGetItem` | 1 |
| `LockUserSession` | 1 |
| `MemoryEditor` | 1 |
| `NtfsGetVolumeGeometry` | 1 |
| `PackageManifestCheckCompatibility` | 1 |
| `PackageManifestFindCommandTarget` | 1 |
| `PathCompletionInitA` | 1 |
| `PathCompletionNext` | 1 |
| `PostMessage` | 1 |
| `ProfileDump` | 1 |
| `RebootKernel` | 1 |
| `RecordUserAuthenticationFailure` | 1 |
| `RecordUserAuthenticationSuccess` | 1 |
| `ReservedHeapDeinit` | 1 |
| `ReservedHeapInit` | 1 |
| `ReservedHeapInitAllocator` | 1 |
| `ResetThemeToDefault` | 1 |
| `RunSingleTestByName` | 1 |
| `ScriptCreateContextA` | 1 |
| `ScriptDestroyContext` | 1 |
| `ScriptGetReturnValue` | 1 |
| `ScriptIsE0FileName` | 1 |
| `SelectKeyboard` | 1 |
| `SessionUserRequiresPassword` | 1 |
| `ShutdownKernel` | 1 |
| `Spawn` | 1 |
| `StringArrayAddUnique` | 1 |
| `StringArrayDeinit` | 1 |
| `StringArrayGet` | 1 |
| `StringArrayInitA` | 1 |
| `StringFindCharR` | 1 |
| `SystemDataViewMode` | 1 |
| `TomlGet` | 1 |
| `UpdateSessionActivity` | 1 |
| `VerifySessionUnlockPassword` | 1 |
| `WaitKey` | 1 |

## Per File Detail

### `kernel/source/shell/Shell-Commands-Core.c`

- `ConsolePrint`: `41`
- `StringCopy`: `24`
- `StringLength`: `18`
- `ScriptRegisterHostSymbol`: `8`
- `StringConcat`: `8`
- `StringCompareNC`: `6`
- `GetCurrentProcess`: `5`
- `GetSystemFS`: `5`
- `ProcessControlIsInterruptRequested`: `5`
- `StringCompare`: `4`
- `AllocatorFree`: `2`
- `ConsoleSetPagingEnabled`: `2`
- `GetKeyboardCode`: `2`
- `MemoryCopy`: `2`
- `ProcessControlCheckpoint`: `2`
- `ProcessControlConsumeInterrupt`: `2`
- `SizeFormatBytesText`: `2`
- `StringCopyNum`: `2`
- `StringToU32`: `2`
- `AllocatorAlloc`: `1`
- `AllocatorInitProcess`: `1`
- `ClearConsole`: `1`
- `CommandLineEditorDeinit`: `1`
- `CommandLineEditorInitA`: `1`
- `CommandLineEditorSetCompletionCallback`: `1`
- `ConsoleGetPagingEnabled`: `1`
- `GetDiskList`: `1`
- `GetDriverList`: `1`
- `GetKeyboardDescriptor`: `1`
- `GetKeyboardRootHandle`: `1`
- `GetMouseDescriptor`: `1`
- `GetMouseRootHandle`: `1`
- `GetPCIDeviceList`: `1`
- `GetProcessList`: `1`
- `MemorySet`: `1`
- `PathCompletionInitA`: `1`
- `PathCompletionNext`: `1`
- `ReservedHeapDeinit`: `1`
- `ReservedHeapInit`: `1`
- `ReservedHeapInitAllocator`: `1`
- `ScriptCreateContextA`: `1`
- `ScriptDestroyContext`: `1`
- `SelectKeyboard`: `1`
- `StringArrayAddUnique`: `1`
- `StringArrayDeinit`: `1`
- `StringArrayGet`: `1`
- `StringArrayInitA`: `1`
- `StringFindCharR`: `1`
- `StringPrintFormat`: `1`
- `WaitKey`: `1`

### `kernel/source/shell/Shell-Commands-Graphics.c`

- `ConsolePrint`: `57`
- `StringLength`: `14`
- `StringCompareNC`: `12`
- `GetSystemPen`: `5`
- `GraphicsSelectorGetDriver`: `5`
- `SelectPen`: `5`
- `StringCopy`: `4`
- `ActivateTheme`: `3`
- `DeleteDesktop`: `3`
- `SelectBrush`: `3`
- `CreateDesktop`: `2`
- `DesktopDrawText`: `2`
- `DisplaySwitchToDesktop`: `2`
- `GetActiveDesktop`: `2`
- `GetDriverList`: `2`
- `GetGraphicsDriver`: `2`
- `GetSystemBrush`: `2`
- `GraphicsSelectorGetActiveBackendName`: `2`
- `Line`: `2`
- `LoadTheme`: `2`
- `Rectangle`: `2`
- `StringToU32`: `2`
- `BaseWindowFunc`: `1`
- `BeginWindowDraw`: `1`
- `CreateWindow`: `1`
- `DesktopInternalRunStressDrag`: `1`
- `DesktopMeasureText`: `1`
- `DisplaySessionGetActiveFrontEnd`: `1`
- `DisplaySessionSetConsoleGraphicsMode`: `1`
- `DisplaySessionSetDesktopMode`: `1`
- `DisplaySwitchToConsole`: `1`
- `EndWindowDraw`: `1`
- `FontGetDefaultFace`: `1`
- `GetActiveThemeInfo`: `1`
- `GetConfigurationValue`: `1`
- `GraphicsSelectorForceBackendByName`: `1`
- `PostMessage`: `1`
- `ResetThemeToDefault`: `1`
- `Sleep`: `1`

### `kernel/source/shell/Shell-Commands-Package.c`

- `ConsolePrint`: `38`
- `StringCopy`: `14`
- `PackageManifestRelease`: `13`
- `StringConcat`: `9`
- `StringCompareNC`: `7`
- `PackageFSUnmount`: `6`
- `StringLength`: `5`
- `PackageManifestStatusToString`: `4`
- `PackageNamespaceUnbindCurrentProcessPackageView`: `4`
- `StringPrintFormat`: `4`
- `FileReadAll`: `3`
- `KernelPathBuildFile`: `3`
- `PackageManifestParseFromPackageBuffer`: `3`
- `KernelPathResolve`: `2`
- `PackageFSMountFromBuffer`: `2`
- `PackageNamespaceBindCurrentProcessPackageView`: `2`
- `AllocatorAlloc`: `1`
- `AllocatorFree`: `1`
- `CreateProcess`: `1`
- `FileWriteAll`: `1`
- `MemorySet`: `1`
- `PackageManifestCheckCompatibility`: `1`
- `PackageManifestFindCommandTarget`: `1`
- `ScriptExecute`: `1`
- `ScriptGetErrorMessage`: `1`
- `ScriptGetReturnValue`: `1`
- `ScriptIsE0FileName`: `1`
- `SizeFormatBytesText`: `1`
- `Spawn`: `1`
- `StringCompare`: `1`
- `StringFindChar`: `1`

### `kernel/source/shell/Shell-Commands-Storage.c`

- `ConsolePrint`: `52`
- `SizeFormatBytesText`: `6`
- `StringLength`: `3`
- `Edit`: `2`
- `FileSystemGetPartitionFormatName`: `2`
- `FileSystemGetPartitionTypeName`: `2`
- `MemorySet`: `2`
- `StringEmpty`: `2`
- `AllocatorAlloc`: `1`
- `AllocatorFree`: `1`
- `FileReadAll`: `1`
- `FileSystemGetPartitionSchemeName`: `1`
- `FileSystemGetStorageUnit`: `1`
- `FileWriteAll`: `1`
- `GetDiskList`: `1`
- `GetFileSystemGlobalInfo`: `1`
- `GetFileSystemList`: `1`
- `GetUnusedFileSystemList`: `1`
- `NtfsGetVolumeGeometry`: `1`
- `StringConcat`: `1`
- `StringCopy`: `1`

### `kernel/source/shell/Shell-Commands-System.c`

- `ConsolePrint`: `73`
- `StringCompareNC`: `14`
- `StringLength`: `10`
- `MemorySet`: `8`
- `StringToU32`: `4`
- `GetDriverList`: `3`
- `KernelEnumGetProvider`: `3`
- `KernelEnumNext`: `3`
- `DriverTypeToText`: `2`
- `SizeFormatBytesText`: `2`
- `XHCIEnumErrorToString`: `2`
- `Disassemble`: `1`
- `DriverDomainToText`: `1`
- `GetNetworkDeviceList`: `1`
- `GetProcessMemoryRegionList`: `1`
- `GetTaskList`: `1`
- `GetUsbStorageList`: `1`
- `KernelEnumPretty`: `1`
- `KillTaskForCurrentProcess`: `1`
- `ListGetItem`: `1`
- `MemoryEditor`: `1`
- `ProfileDump`: `1`
- `RebootKernel`: `1`
- `RunSingleTestByName`: `1`
- `ShutdownKernel`: `1`
- `SystemDataViewMode`: `1`

### `kernel/source/shell/Shell-Commands-Users.c`

- `ConsolePrint`: `36`
- `StringCopy`: `8`
- `StringLength`: `6`
- `CommandLineEditorReadLine`: `5`
- `ConsoleGetString`: `4`
- `GetCurrentSession`: `4`
- `FindUserAccountByID`: `3`
- `DestroyUserSession`: `2`
- `SaveUserDatabase`: `2`
- `SetCurrentSession`: `2`
- `StringCompareNC`: `2`
- `VerifyPassword`: `2`
- `CanAttemptUserAuthentication`: `1`
- `ChangeUserPassword`: `1`
- `CreateUserAccount`: `1`
- `CreateUserSession`: `1`
- `DeleteUserAccount`: `1`
- `FindUserAccount`: `1`
- `GetCurrentTask`: `1`
- `GetLocalTime`: `1`
- `GetUserAccountList`: `1`
- `RecordUserAuthenticationFailure`: `1`
- `RecordUserAuthenticationSuccess`: `1`
- `Sleep`: `1`
- `StringCompare`: `1`

### `kernel/source/shell/Shell-Execution.c`

- `ConsolePrint`: `4`
- `StringCompareNC`: `2`
- `StringCopy`: `2`
- `StringFindChar`: `2`
- `StringPrintFormat`: `2`
- `CommandLineEditorReadLine`: `1`
- `CommandLineEditorRemember`: `1`
- `ConsoleResetPaging`: `1`
- `GetConfiguration`: `1`
- `GetCurrentSession`: `1`
- `MemorySet`: `1`
- `ScriptExecute`: `1`
- `ScriptGetErrorMessage`: `1`
- `Sleep`: `1`
- `StringLength`: `1`
- `TomlGet`: `1`
- `UpdateSessionActivity`: `1`

### `kernel/source/shell/Shell-Main.c`

- `ConsolePrint`: `24`
- `Sleep`: `4`
- `CommandLineEditorSetIdleCallback`: `3`
- `IsUserSessionLocked`: `3`
- `DestroyUserSession`: `2`
- `FindUserAccountByID`: `2`
- `GetCurrentSession`: `2`
- `GetUserAccountList`: `2`
- `StringEmpty`: `2`
- `UnlockUserSession`: `2`
- `CanAttemptSessionUnlock`: `1`
- `ClearConsole`: `1`
- `CommandLineEditorReadLine`: `1`
- `ConsoleCaptureActiveRegionSnapshot`: `1`
- `ConsolePrintChar`: `1`
- `ConsoleReleaseActiveRegionSnapshot`: `1`
- `ConsoleRestoreActiveRegionSnapshot`: `1`
- `CreateUserSession`: `1`
- `FindUserAccount`: `1`
- `GetCurrentTask`: `1`
- `GetDoLogin`: `1`
- `GetShowDesktop`: `1`
- `IsUserSessionTimedOut`: `1`
- `LockUserSession`: `1`
- `SessionUserRequiresPassword`: `1`
- `SetCurrentSession`: `1`
- `StringLength`: `1`
- `StringPrintFormat`: `1`
- `VerifyPassword`: `1`
- `VerifySessionUnlockPassword`: `1`
