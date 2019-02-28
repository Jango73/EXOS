
// ECC_VDrive.cpp

/*************************************************************************************************\

	Exelsius Common Classes
	Copyright (c) 1999 Exelsius
	All rights reserved

\*************************************************************************************************/

#include <ECC_Def.h>
#include <ECC.h>

/*************************************************************************************************\

	Information on cluster bitmaps

	Bytes allocated		Clusters mapped		Total bytes on drive

	1,024				8,192				8,388,608
	2,048				16,384				16,777,216
	4,096				32,768				33,554,432
	8,192				65,536				67,108,864
	16,384				131,072				134,217,728
	32,768				262,144				268,435,456
	65,536				524,288				536,870,912

\*************************************************************************************************/

#define USE_CLUSTER_BUFFERS

#define CLEAR_CLUSTER(c) { memset(c, 0, VD_CLUSTER_SIZE); }

/*************************************************************************************************/

#ifdef USE_CLUSTER_BUFFERS

#pragma message ("class VirtualDrive : Using cluster buffers")

#else

#pragma message ("class VirtualDrive : Using direct file access (no cluster buffers)")

#endif

/*************************************************************************************************/

static const char SysFolderName_Self []		= ".";
static const char SysFolderName_Parent []	= "..";

/*************************************************************************************************/

static String StringToVDPath (const String& FileName)
{
	U32 Size = FileName.Size();
	if (Size > VD_MAX_PATHNAME) return "";

	char szTemp [VD_MAX_PATHNAME];
	strcpy(szTemp, FileName());

	for (U32 c = 0; c < Size; c++) if (szTemp[c] == '\\') szTemp[c] = '/';

	return String(szTemp);
}

/*************************************************************************************************/

static int IsValidFileName (const String& Name)
{
	U32 Size = Name.Size();
	if (Size == 0 || Size > VD_MAX_FILENAME) return 0;

	char szTemp [VD_MAX_PATHNAME + 1];
	strcpy(szTemp, Name());

	for (U32 Index = 0; Index < Size; Index++)
	{
		if (szTemp[Index] <  ' ') return 0;
		if (szTemp[Index] == '"') return 0;		// File name delimiter
		if (szTemp[Index] == '!') return 0;		// Deleted file character
		if (szTemp[Index] == '*') return 0;		// Joker
		if (szTemp[Index] == '?') return 0;		// Joker
		if (szTemp[Index] == '=') return 0;		// Reserved
		if (szTemp[Index] == '#') return 0;		// Reserved
		if (szTemp[Index] == '(') return 0;		// Reserved
		if (szTemp[Index] == ')') return 0;		// Reserved
		if (szTemp[Index] == '{') return 0;		// Reserved
		if (szTemp[Index] == '}') return 0;		// Reserved
		if (szTemp[Index] == '[') return 0;		// Reserved
		if (szTemp[Index] == ']') return 0;		// Reserved
	}

	return 1;
}

/*************************************************************************************************/

static String GetFilePath (const String& FileName)
{
	U32 Size = FileName.Size();
	if (Size > VD_MAX_PATHNAME) return "";

	if (FileName == "/")
	{
		// Error : Root must be specified with '//'
		return FileName;
	}

	char szTemp [VD_MAX_PATHNAME];
	strcpy(szTemp, FileName());

	char* Slash = strrchr(szTemp, '/');
	if (Slash) *Slash = '\0';

	return String(szTemp);
}

/*************************************************************************************************/

static String GetFileName (const String& FileName)
{
	U32 Size = FileName.Size();
	if (Size > VD_MAX_PATHNAME) return "";

	if (FileName == "/")
	{
		// Error : Root must be specified with '//'
		return FileName;
	}

	char szTemp [VD_MAX_PATHNAME];
	strcpy(szTemp, FileName());

	char* Slash = strrchr(szTemp, '/');
	if (Slash) return String(Slash + 1);

	return "";
}

/*************************************************************************************************/

static int FileConcerned (const char* Name, const char* Specs)
{
	return 1;

	// Check if all files are concerned
	if (strcmp(Specs, "*") == 0) return 1;

	const char* Idx1 = Name;
	const char* Idx2 = Name;

	while (1)
	{
		if (*Idx1 == '\0' && *Idx2 == '\0') return 1;

		if (tolower(*Idx1) != tolower(*Idx2))
		{
			if (*Idx2 == '?') break;
			if (*Idx2 == '*')
			{
				while (*Idx1 != '.' && *Idx1 != '\0') Idx1++;
				while (*Idx2 != '.' && *Idx2 != '\0') Idx2++;
			}
		}

		Idx1++;
		Idx2++;
	}

	return 0;
}

/*************************************************************************************************/

// Implementation of class : VirtualDrive

IMPLEMENT_CLASS (VirtualDrive, Object)

/*************************************************************************************************/

ECCAPI VirtualDrive :: VirtualDrive (const String& UserFileName, int AllowCreation)
{
	// Clear everything
	hFile = INVALID_HANDLE_VALUE;

	memset(&SuperBlock,	0, sizeof (SuperBlock));
	memset(FileStruct,	0, sizeof (FileStruct));
	memset(FindStruct,	0, sizeof (FindStruct));
	memset(Clusters,	0, sizeof (Clusters));

	// Assign the name of the file containing the virtual drive
	FileName = UserFileName;

	// Try to open an existing virtual drive
	hFile = ::CreateFile
	(
		FileName(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
		NULL
	);

	// If it does not exist, create and format
	if (hFile == INVALID_HANDLE_VALUE)
	{
		if (AllowCreation)
		{
			hFile = ::CreateFile
			(
				FileName(),
				GENERIC_READ | GENERIC_WRITE,
				0,
				NULL,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
				NULL
			);

			if (hFile != INVALID_HANDLE_VALUE)
			{
				U8              Buffer [VD_CLUSTER_SIZE];
				U32             CurrentCluster = 0;
				LPVD_FILERECORD FileRecord     = (LPVD_FILERECORD) Buffer;

				//-------------------------------------
				// Write the boot cluster
				CLEAR_CLUSTER(Buffer);
				WriteCluster(CurrentCluster, Buffer);

				CurrentCluster++;

				//-------------------------------------
				// Fill the SuperBlock
				SuperBlock.Magic				= VD_MAGIC;
				SuperBlock.Version				= VD_VERSION_CURRENT;
				SuperBlock.ClusterSize			= VD_CLUSTER_SIZE;
				SuperBlock.NumClusters			= 5;
				SuperBlock.ClusterBitmap		= 2;
				SuperBlock.ClusterBitmapSize	= 1;
				SuperBlock.Root					= 3;
				SuperBlock.OS					= 0;
				SuperBlock.MaxMountCount		= 256;
				SuperBlock.MountCount			= 0;

				memset(SuperBlock.CreatorName, 0, sizeof (SuperBlock.CreatorName));
				memset(SuperBlock.Password, 0, sizeof (SuperBlock.Password));

				strcpy(SuperBlock.CreatorName, "Exelsius Common Classes - (c) 1999 Exelsius");

				// Write the SuperBlock
				CLEAR_CLUSTER(Buffer);
				memcpy(Buffer, &SuperBlock, sizeof (SuperBlock));
				WriteCluster(CurrentCluster, Buffer);

				CurrentCluster++;

				//-------------------------------------
				// Create the cluster bitmap
				CLEAR_CLUSTER(Buffer);

				// Mark the first 5 clusters as used
				Buffer[0] = 0x1F;

				WriteCluster(CurrentCluster, Buffer);

				CurrentCluster++;

				//-------------------------------------
				// Create the root FileRecord
				U32 RootCluster = CurrentCluster;

				CLEAR_CLUSTER(Buffer);
				InitFileRecord(FileRecord);

				FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
				FileRecord->Name[0]			= '/';
				FileRecord->Name[1]			= '\0';
				FileRecord->ClusterTable	= CurrentCluster + 1;

				WriteCluster(CurrentCluster, Buffer);

				CurrentCluster++;

				// Create the root folder
				// It contains the helper '.' and '..' files
				CLEAR_CLUSTER(Buffer);

				InitFileRecord(FileRecord);
				FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
				FileRecord->ClusterTable	= CurrentCluster;
				FileRecord->Sibling			= VD_FR_NEXTVALID;

				strcpy(FileRecord->Name, SysFolderName_Self);

				FileRecord++;

				InitFileRecord(FileRecord);
				FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
				FileRecord->ClusterTable	= CurrentCluster;
				FileRecord->Sibling			= VD_FR_END;

				strcpy(FileRecord->Name, SysFolderName_Parent);

				WriteCluster(CurrentCluster, Buffer);

				CurrentCluster++;
			}
			else
			{
				// MessageBox(NULL, "Failed to create !", NULL, MB_OK);
			}
		}
	}
	else
	{
		// Read the SuperBlock
		U8	Buffer [VD_CLUSTER_SIZE];

		if (ReadCluster(1, Buffer))
		{
			memcpy(&SuperBlock, Buffer, sizeof (VD_SUPERBLOCK));

			// Update the mount count
			SuperBlock.MountCount++;
		}
		else
		{
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
		}
	}
}

/*************************************************************************************************/

ECCAPI VirtualDrive :: ~VirtualDrive ()
{
	if (IsDriveValid())
	{
		// Update the SuperBlock
		U8 Buffer [VD_CLUSTER_SIZE];
		CLEAR_CLUSTER(Buffer);
		memcpy(Buffer, &SuperBlock, sizeof (VD_SUPERBLOCK));
		WriteCluster(VD_CLUSTER_SUPERBLOCK, Buffer);

		U32 Index = 0;

		// Close all open files
		for (Index = 1; Index < VD_MAX_FILES; Index++)
		{
			if (FileStruct[Index].Flags & VD_FS_USED)
			{
				CloseFile(Index);
			}
		}

#ifdef USE_CLUSTER_BUFFERS

		// Flush all buffers
		for (Index = 0; Index < VD_MAX_BUFFERS; Index++)
		{
			if ((Clusters[Index].Flags & VD_CB_VALID) && (Clusters[Index].Flags & VD_CB_MODIFIED))
			{
				FlushCluster(Clusters[Index].Cluster, Clusters[Index].Buffer);
			}
		}

#endif

		// Close the Windows file
		::CloseHandle(hFile);
	}
}

/*************************************************************************************************/

ECCAPI const String& VirtualDrive :: GetContainerName ()
{
	return FileName;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: IsValid ()
{
	return IsDriveValid();
}

/*************************************************************************************************/

ECCAPI String VirtualDrive :: ToString () const
{
	return "";
}

/*************************************************************************************************/

ECCAPI VirtualDrive& VirtualDrive :: operator = (const VirtualDrive& Target)
{
	Object :: operator = (Target);

	return *this;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: CreateFolder (const String& FileName)
{
	if (IsDriveValid() == 0) return 0;

	String Path = StringToVDPath(FileName);

	// Check if file already exists
	VD_FILELOC Loc = LocateFileRecord(Path);
	if (Loc.Cluster) return 0;

	String	ParentName	= GetFilePath(Path);
	String	FolderName	= GetFileName(Path);

	if (IsValidFileName(FolderName) == 0) return 0;

	U8	Buffer [VD_CLUSTER_SIZE];

	LPVD_FILERECORD FileRecord = NULL;

	VD_FILELOC	Parent;
	VD_FILELOC	Folder;
	U32			ParentClusterTable;
	U32			FolderClusterTable;

	// Locate file record of parent folder
	Parent = LocateFileRecord(ParentName);
	if (Parent.Cluster == 0) return 0;

	// Read FileRecord table containing parent folder
	if (ReadCluster(Parent.Cluster, Buffer) == 0) return 0;
	FileRecord = ((LPVD_FILERECORD) Buffer) + Parent.Offset;

	// Get FileRecord table of parent folder
	ParentClusterTable = FileRecord->ClusterTable;
	if (ParentClusterTable == 0) return 0;

	// Get a new FileRecord for the new folder
	Folder = GetNewFileRecord(ParentClusterTable);
	if (Folder.Cluster == 0) return 0;

	// Get a new cluster for the new folder entries
	FolderClusterTable = CreateNewCluster();
	if (FolderClusterTable == 0) return 0;

	// Read the FileRecord table of the folder
	if (ReadCluster(Folder.Cluster, Buffer) == 0) return 0;

	// Fill the FileRecord of the new folder
	FileRecord = ((LPVD_FILERECORD) Buffer) + Folder.Offset;

	FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
	FileRecord->ClusterTable	= FolderClusterTable;

	strcpy(FileRecord->Name, FolderName());

	if (WriteCluster(Folder.Cluster, Buffer) == 0) return 0;

	// Initialize the new folder
	// Create the helper '.' and '..' entries
	FileRecord = (LPVD_FILERECORD) Buffer;
	CLEAR_CLUSTER(Buffer);

	InitFileRecord(FileRecord);
	FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
	FileRecord->ClusterTable	= FolderClusterTable;
	FileRecord->Sibling			= VD_FR_NEXTVALID;

	strcpy(FileRecord->Name, SysFolderName_Self);

	FileRecord++;

	InitFileRecord(FileRecord);
	FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_FOLDER;
	FileRecord->ClusterTable	= ParentClusterTable;
	FileRecord->Sibling			= VD_FR_END;

	strcpy(FileRecord->Name, SysFolderName_Parent);

	if (WriteCluster(FolderClusterTable, Buffer) == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: DeleteFolder (const String& FileName)
{
	if (IsDriveValid() == 0) return 0;

	String Path = StringToVDPath(FileName);

	U8				Buffer [VD_CLUSTER_SIZE];
	LPVD_FILERECORD	FileRecord = NULL;
	VD_FILELOC		Parent;
	VD_FILELOC		Folder;
	U32				ParentClusterTable;
	U32				FolderClusterTable;
	U32				ClusterTable;

	// Locate the file
	Folder = LocateFileRecord(Path);
	if (Folder.Cluster == 0) return 0;

	if (Folder.Cluster == Folder.MainCluster)
	{
		// Check if user wants to delete "." or ".."
		if (Folder.Offset == 0 || Folder.Offset == 1) return 0;
	}

	// Read the folder's file record
	if (ReadCluster(Folder.Cluster, Buffer) == 0) return 0;
	FileRecord = ((LPVD_FILERECORD) Buffer) + Folder.Offset;

	// Get the folder's cluster table
	FolderClusterTable = FileRecord->ClusterTable;
	if (FolderClusterTable == 0) return 0;

	// Check that folder is empty
	{
		ClusterTable = FolderClusterTable;

		// Read the FileRecord table of the folder's entries
		if (ReadCluster(ClusterTable, Buffer) == 0) return 0;

		FileRecord		= (LPVD_FILERECORD) Buffer;
		ClusterTable	= FolderClusterTable;

		while (1)
		{
			if ((FileRecord->Attributes & VD_ATTR_FREE) == 0)
			{
				if (stricmp(FileRecord->Name, SysFolderName_Self) == 0)
				{
				}
				else
				if (stricmp(FileRecord->Name, SysFolderName_Parent) == 0)
				{
				}
				else
				{
					// Error : folder is not empty
					return 0;
				}
			}

			if (FileRecord->Sibling == VD_FR_END) break;
			else
			if (FileRecord->Sibling == VD_FR_NEXTVALID) FileRecord++;
			else
			{
				ClusterTable = FileRecord->Sibling;
				if (ClusterTable == 0) return 0;
				if (ReadCluster(ClusterTable, Buffer) == 0) return 0;
				FileRecord = (LPVD_FILERECORD) Buffer;
			}
		}
	}

	// Read the folder's file record
	if (ReadCluster(Folder.Cluster, Buffer) == 0) return 0;
	FileRecord = ((LPVD_FILERECORD) Buffer) + Folder.Offset;

	// Clear the folder's entry
	FileRecord->Size			= 0;
	FileRecord->SizeReserved	= 0;
	FileRecord->Attributes		= VD_ATTR_FREE;
	FileRecord->Name[0]			= '!';
	FileRecord->Name[1]			= '!';
	FileRecord->Name[2]			= '\0';

	// Mark the folder's ClusterTable as unsused
	MarkCluster(FolderClusterTable, 0);

	// Write back the folder's file record
	if (WriteCluster(Folder.Cluster, Buffer) == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: CopyFile (const String& SourceName, const String& DestName)
{
	if (IsDriveValid() == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: MoveFile (const String& SourceName, const String& DestName)
{
	if (IsDriveValid() == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: OpenFile (const String& FullName, U32 Mode, U32 Flags)
{
	if (IsDriveValid() == 0) return 0;

	U8 Buffer [VD_CLUSTER_SIZE];
	LPVD_FILERECORD FileRecord = NULL;

	String Path = GetFilePath(FullName);
	String Name = GetFileName(FullName);

	if (IsValidFileName(Name) == 0) return 0;

	// Check consistency of mode and flags
	if ((Mode & VD_READ) && (Flags & VD_CREATE_ALWAYS)) return 0;

	// Check if the file is already open
	for (U32 Index = 1; Index < VD_MAX_FILES; Index++)
	{
		if (FileStruct[Index].Flags & VD_FS_USED)
		{
			if (stricmp((char*) FileStruct[Index].Name, FullName()) == 0) return 0;
		}
	}

	// Get a free FILESTRUCT to access the file
	U32 Handle = GetNewFileHandle();
	if (Handle == 0xFFFFFFFF) return 0;

	LPVD_FILESTRUCT File = FileStruct + Handle;

	// Locate file on the drive
	VD_FILELOC Loc = LocateFileRecord(FullName);

	// Check if file must be created
	if (Loc.Cluster == 0)
	{
		if (Flags & VD_OPEN_EXISTING) return 0;

		if (Flags & VD_CREATE_ALWAYS)
		{
			if (Path.Empty()) return 0;

			// Get the FileRecord of the parent folder
			VD_FILELOC Parent = LocateFileRecord(Path);
			if (Parent.Cluster == 0) return 0;

			if (ReadCluster(Parent.Cluster, Buffer) == 0) return 0;
			FileRecord = ((LPVD_FILERECORD) Buffer) + Parent.Offset;

			// Check if this actually is a folder
			if ((FileRecord->Attributes & VD_ATTR_FOLDER) == 0) return 0;

			// Get the ClusterTable of this folder
			U32 ParentClusterTable = FileRecord->ClusterTable;
			if (ParentClusterTable == 0) return 0;

			// Insert a new FileRecord in the parent's ClusterTable
			Loc = GetNewFileRecord(ParentClusterTable);
			if (Loc.Cluster == 0) return 0;

			// Get a new cluster to store this file's ClusterTable
			U32 FileClusterTable = CreateNewCluster();
			if (FileClusterTable == 0) return 0;

			// Set the first entry to CT_END to indicate file is empty
			if (ReadCluster(FileClusterTable, Buffer) == 0) return 0;
			U32* ClusterEntry = (U32*) Buffer;
			ClusterEntry[0] = VD_CT_END;
			if (WriteCluster(FileClusterTable, Buffer) == 0) return 0;

			if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
			FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

			// Initialize this file's FileRecord
			FileRecord->Attributes		= VD_ATTR_READ | VD_ATTR_WRITE;
			FileRecord->ClusterTable	= FileClusterTable;
			// FileRecord->Sibling			= VD_FR_END;

			strcpy(FileRecord->Name, Name());

			// Update this file's FileRecord
			if (WriteCluster(Loc.Cluster, Buffer) == 0) return 0;
		}
	}
	else
	{
		// Read the file record
		if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
		FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

		// Check that this is not a folder
		if (FileRecord->Attributes & VD_ATTR_FOLDER) return 0;

		// Is writing granted ?
		if (Mode & VD_WRITE)
		{
			if ((FileRecord->Attributes & VD_ATTR_WRITE) == 0) return 0;
		}

		// Is reading granted ?
		// Read-disabled files are reserved to kernel
		if (Mode & VD_READ)
		{
			if ((FileRecord->Attributes & VD_ATTR_READ) == 0) return 0;
		}

		// Erase file's data if necessary
		if (Flags & VD_CREATE_ALWAYS)
		{
			if (ClearFileClusters(Loc, 0) == 0) return 0;

			// Set file size to 0
			FileRecord->Size			= 0;
			FileRecord->SizeReserved	= 0;

			// Write back the file record
			if (WriteCluster(Loc.Cluster, Buffer) == 0) return 0;
		}
	}

	// Read the file record
	if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
	FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

	// Initialize the FILESTRUCT
	File->Flags					|= VD_FS_USED;
	File->Mode					= Mode;
	File->Location				= Loc;
	File->Record				= (*FileRecord);
	File->Position.Table		= File->Record.ClusterTable;
	File->Position.Index		= 0;
	File->Position.Bytes		= 0;

	// Update time stamps
	File->Record.Time_Accessed = GetCurrentTime();
	if (Mode & VD_WRITE)
	{
		File->Record.Time_Modified = File->Record.Time_Accessed;
	}

	strcpy(FileStruct->Name, FullName());

	// Move to end of file if append mode is on
	if ((Mode & VD_WRITE) && (Mode & VD_APPEND))
	{
		// Read the start of the ClusterTable
		if (ReadCluster(File->Position.Table, Buffer) == 0) return 0;
		U32* Table = (U32*) Buffer;

		while (1)
		{
			if (Table[File->Position.Index] == VD_CT_END) break;
			else
			if (File->Position.Index == (VD_MAX_ENTRYINCLUSTER - 1))
			{
				File->Position.Table = Table[File->Position.Index];
				if (ReadCluster(File->Position.Table, Buffer) == 0) return 0;
				File->Position.Index = 0;
			}
			else
			{
				File->Position.Index++;
			}
		}

		File->Position.Bytes = File->Record.Size;
	}

	return Handle;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: CloseFile (U32 Handle)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0;

	U8 Buffer [VD_CLUSTER_SIZE];
	LPVD_FILERECORD FileRecord	= NULL;
	LPVD_FILESTRUCT File		= FileStruct + Handle;

	// Update file size
	// U32 FileSize = ComputeFileSize(File->Location);
	// File->Record.Size = FileSize;

	VD_FILELOC Loc = File->Location;

	if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
	FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

	(*FileRecord) = File->Record;

	if (WriteCluster(Loc.Cluster, Buffer) == 0) return 0;

	// Invalidate handle
	File->Flags &= (~VD_FS_USED);

	return 1;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: WriteFile (U32 Handle, const void* UserBuffer, U32 Size)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0;
	if (UserBuffer == NULL) return 0;

	return TransferFile(Handle, const_cast<void*> (UserBuffer), Size, VD_WRITE);
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: ReadFile (U32 Handle, void* UserBuffer, U32 Size)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0;
	if (UserBuffer == NULL) return 0;

	return TransferFile(Handle, UserBuffer, Size, VD_READ);
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: GetFileSize (U32 Handle)
{
	if (IsDriveValid() == 0) return VD_INVALID_SIZE;
	if (IsFileHandleValid(Handle) == 0) return VD_INVALID_SIZE;

	LPVD_FILESTRUCT File = FileStruct + Handle;

	return File->Record.Size;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: GetFileAttributes (U32 Handle)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0xFFFFFFFF;

	LPVD_FILESTRUCT	File		= FileStruct + Handle;
	U32				Attributes	= 0;

	if (File->Record.Attributes & VD_ATTR_READ)		Attributes |= VD_ATTR_READ;
	if (File->Record.Attributes & VD_ATTR_WRITE)	Attributes |= VD_ATTR_WRITE;
	if (File->Record.Attributes & VD_ATTR_HIDDEN)	Attributes |= VD_ATTR_HIDDEN;
	if (File->Record.Attributes & VD_ATTR_ARCHIVE)	Attributes |= VD_ATTR_ARCHIVE;

	return Attributes;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: SetFileAttributes (U32 Handle, U32 Attributes)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: GetFileTime (U32 Handle, LPVD_FILETIME Time)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return 0;

	Time[0] = FileStruct[Handle].Record.Time_Creation;
	Time[1] = FileStruct[Handle].Record.Time_Accessed;
	Time[2] = FileStruct[Handle].Record.Time_Modified;

	return 1;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: GetFilePointer (U32 Handle)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return VD_INVALID_SIZE;

	return FileStruct[Handle].Position.Bytes;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: SetFilePointer (U32 Handle, U32 Offset, U32 From)
{
	if (IsDriveValid() == 0) return 0;
	if (IsFileHandleValid(Handle) == 0) return VD_INVALID_SIZE;

	FileStruct[Handle].Position.Bytes = 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: DeleteFile (const String& FileName)
{
	if (IsDriveValid() == 0) return 0;

	String FullName = StringToVDPath(FileName);

	VD_FILELOC Loc = LocateFileRecord(FullName);
	if (Loc.Cluster == 0) return 0;

	U8 Buffer [VD_CLUSTER_SIZE];
	if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
	LPVD_FILERECORD FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

	// Mark all the file's clusters as unused
	if (ClearFileClusters(Loc, 0) == 0) return 0;

	// Erase the file's entry
	FileRecord->Size			= 0;
	FileRecord->SizeReserved	= 0;
	FileRecord->Attributes		= VD_ATTR_FREE;
	FileRecord->Name[0]			= '!';
	FileRecord->Name[1]			= '!';
	FileRecord->Name[2]			= '\0';

	if (WriteCluster(Loc.Cluster, Buffer) == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: RenameFile (const String& OldFileName, const String& NewFileName)
{
	if (IsDriveValid() == 0) return 0;

	String FullName = StringToVDPath(OldFileName);
	String NewName  = StringToVDPath(NewFileName);

	return 1;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: FindFirstFile (const String& RawSpecs, LPVD_FINDDATA UserData)
{
	if (IsDriveValid() == 0) return 0;

	if (RawSpecs.Size() > VD_MAX_PATHNAME) return 0;

	String	Specs	= StringToVDPath(RawSpecs);
	String	Path	= GetFilePath(Specs);
	String	Name	= GetFileName(Specs);

	if (Name == "") return 0;

	for (U32 Handle = 1; Handle < VD_MAX_FINDS; Handle++)
	{
		if ((FindStruct[Handle].Flags & VD_FS_USED) == 0)
		{
			strcpy(FindStruct[Handle].Path, Path());
			strcpy(FindStruct[Handle].Name, Name());

			FindStruct[Handle].Location.MainCluster	= 0;
			FindStruct[Handle].Location.Cluster		= 0;
			FindStruct[Handle].Location.Offset		= 0;

			VD_FILELOC Loc = LocateFileRecord(Path);
			if (Loc.Cluster == 0) return 0;

			U8 Buffer [VD_CLUSTER_SIZE];
			if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
			LPVD_FILERECORD FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

			if ((FileRecord->Attributes & VD_ATTR_FOLDER) == 0) return 0;

			Loc.MainCluster	= FileRecord->ClusterTable;
			Loc.Cluster		= Loc.MainCluster;
			Loc.Offset		= 0;
			if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
			FileRecord = (LPVD_FILERECORD) Buffer;

			while (1)
			{
				if ((FileRecord->Attributes & VD_ATTR_FREE) == 0 && FileConcerned(FileRecord->Name, Specs()))
				{
					FindStruct[Handle].Location = Loc;

					strcpy(UserData->Name, Path());
					strcat(UserData->Name, "/");
					strcat(UserData->Name, FileRecord->Name);

					UserData->CreationTime		= FileRecord->Time_Creation;
					UserData->LastAccessTime	= FileRecord->Time_Accessed;
					UserData->LastModifiedTime	= FileRecord->Time_Modified;

					UserData->Size				= FileRecord->Size;
					UserData->Attributes		= FileRecord->Attributes;

					FindStruct[Handle].Flags |= VD_FS_USED;

					return Handle;
				}

				// Proceed to next file
				if (FileRecord->Sibling == VD_FR_NEXTVALID) { FileRecord++; Loc.Offset++; }
				else
				if (FileRecord->Sibling == VD_FR_END) { return 0; }
				else
				{
					Loc.Cluster = FileRecord->Sibling;
					if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
					FileRecord = (LPVD_FILERECORD) Buffer;
					Loc.Offset = 0;
				}
			}
		}
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: FindNextFile (U32 Handle, LPVD_FINDDATA UserData)
{
	if (IsDriveValid() == 0) return 0;

	if (Handle && Handle < VD_MAX_FINDS && (FindStruct[Handle].Flags & VD_FS_USED))
	{
		VD_FILELOC Loc = FindStruct[Handle].Location;

		U8 Buffer [VD_CLUSTER_SIZE];
		if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
		LPVD_FILERECORD FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

		// Proceed to next file
		if (FileRecord->Sibling == VD_FR_NEXTVALID) { FileRecord++; Loc.Offset++; }
		else
		if (FileRecord->Sibling == VD_FR_END) { return 0; }
		else
		{
			Loc.Cluster = FileRecord->Sibling;
			if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
			FileRecord = (LPVD_FILERECORD) Buffer;
			Loc.Offset = 0;
		}

		while (1)
		{
			if ((FileRecord->Attributes & VD_ATTR_FREE) == 0 && FileConcerned(FileRecord->Name, FindStruct[Handle].Name))
			{
				FindStruct[Handle].Location	= Loc;

				strcpy(UserData->Name, FindStruct[Handle].Path);
				strcat(UserData->Name, "/");
				strcat(UserData->Name, FileRecord->Name);

				UserData->CreationTime		= FileRecord->Time_Creation;
				UserData->LastAccessTime	= FileRecord->Time_Accessed;
				UserData->LastModifiedTime	= FileRecord->Time_Modified;

				UserData->Size				= FileRecord->Size;
				UserData->Attributes		= FileRecord->Attributes;

				return 1;
			}

			// Proceed to next file
			if (FileRecord->Sibling == VD_FR_NEXTVALID) { FileRecord++; Loc.Offset++; }
			else
			if (FileRecord->Sibling == VD_FR_END) { return 0; }
			else
			{
				Loc.Cluster = FileRecord->Sibling;
				if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
				FileRecord = (LPVD_FILERECORD) Buffer;
				Loc.Offset = 0;
			}
		}

		return 1;
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: FindClose (U32 Handle)
{
	if (IsDriveValid() == 0) return 0;

	if (Handle && Handle < VD_MAX_FINDS && (FindStruct[Handle].Flags & VD_FS_USED))
	{
		FindStruct[Handle].Flags = 0;
		return 1;
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: Defrag ()
{
	if (IsDriveValid() == 0) return 0;

	return 1;
}

/*************************************************************************************************/

ECCAPI int VirtualDrive :: SetMaxMountCount (U32 Value)
{
	if (IsDriveValid() == 0) return 0;

	SuperBlock.MaxMountCount = Value;

	return 1;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: GetMaxMountCount ()
{
	if (IsDriveValid() == 0) return 0;

	return SuperBlock.MaxMountCount;
}

/*************************************************************************************************/

ECCAPI U32 VirtualDrive :: GetMountCount ()
{
	if (IsDriveValid() == 0) return 0;

	return SuperBlock.MountCount;
}

/*************************************************************************************************/

// Private implementation

/*************************************************************************************************/

U32 VirtualDrive :: TransferFile (U32 Handle, void* UserBuffer, U32 Size, U32 Operation)
{
	// Allocate globals
	LPVD_FILESTRUCT File				= FileStruct + Handle;
	U8				Buffer				[VD_CLUSTER_SIZE];
	U32				BytesTransfered		= 0;
	U32				Count				= File->Position.Bytes / VD_CLUSTER_SIZE;
	U32				BufferOffset		= File->Position.Bytes % VD_CLUSTER_SIZE;
	U32				FreeBytes			= VD_CLUSTER_SIZE - BufferOffset;
	U32*			Table				= (U32*) Buffer;
	U8*				NextBytes			= (U8*) UserBuffer;
	U32				Remaining			= Size;

	// Read the current ClusterTable
	if (ReadCluster(File->Position.Table, Buffer) == 0) return 0;

	while (Remaining)
	{
		if (Table[File->Position.Index] == VD_CT_END)
		{
			if (Operation == VD_WRITE)
			{
				// Do we have to resize the cluster table ?
				if (File->Position.Index == (VD_MAX_ENTRYINCLUSTER - 1))
				{
					// Allocate a new cluster to grow the cluster table
					U32 NewCluster = CreateNewCluster();
					if (NewCluster == 0) goto Out;

					// Update the ClusterTable
					Table[File->Position.Index] = NewCluster;
					if (WriteCluster(File->Position.Table, Buffer) == 0) goto Out;

					// Create a new cluster for file data
					U32	DataCluster = CreateNewCluster();
					if (DataCluster == 0) goto Out;

					// Load new part of cluster table
					File->Position.Table = NewCluster;
					if (ReadCluster(File->Position.Table, Buffer) == 0) goto Out;

					File->Position.Index = 0;

					// Write the first and second entries in this new ClusterTable part
					Table[0]	= DataCluster;
					Table[1]	= VD_CT_END;

					if (WriteCluster(File->Position.Table, Buffer) == 0) goto Out;
				}
				else
				{
					// Create a new cluster for file data
					U32	DataCluster = CreateNewCluster();
					if (DataCluster == 0) goto Out;

					// Update the ClusterTable
					Table[File->Position.Index + 0] = DataCluster;
					Table[File->Position.Index + 1] = VD_CT_END;

					if (WriteCluster(File->Position.Table, Buffer) == 0) goto Out;
				}
			}
			else
			{
				goto Out;
			}
		}
		else
		if (File->Position.Index == (VD_MAX_ENTRYINCLUSTER - 1))
		{
			File->Position.Table = Table[File->Position.Index];
			if (File->Position.Table == 0) goto Out;

			if (ReadCluster(File->Position.Table, Buffer) == 0) goto Out;
			File->Position.Index = 0;
		}

		U32 BytesToTransfer = Remaining;
		if (BytesToTransfer > VD_CLUSTER_SIZE) BytesToTransfer = VD_CLUSTER_SIZE;
		if (BytesToTransfer > FreeBytes) BytesToTransfer = FreeBytes;

		// Read data cluster from disk
		if (ReadCluster(Table[File->Position.Index], IOBuffer) == 0) goto Out;

		if (Operation == VD_WRITE)
		{
			// Transfer user buffer to our temporary buffer
			memcpy(IOBuffer + BufferOffset, NextBytes, BytesToTransfer);

			// Write data cluster to disk
			if (WriteCluster(Table[File->Position.Index], IOBuffer) == 0) goto Out;
		}
		else
		{
			// Transfer temporary buffer to user buffer
			memcpy(NextBytes, IOBuffer + BufferOffset, BytesToTransfer);
		}

		BufferOffset	= 0;
		FreeBytes		= VD_CLUSTER_SIZE;
		NextBytes		+= BytesToTransfer;
		BytesTransfered	+= BytesToTransfer;
		Remaining		-= BytesToTransfer;

		// Next entry in ClusterTable
		File->Position.Index++;
	}

Out :

	// Update file position
	File->Position.Bytes += BytesTransfered;

	// Update file size
	if (Operation == VD_WRITE)
	{
		if (File->Position.Bytes > File->Record.Size)
		{
			File->Record.Size = File->Position.Bytes;
		}
	}

	return BytesTransfered;
}

/*************************************************************************************************/

int VirtualDrive :: ReadCluster (U32 Cluster, U8* Buffer)
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;

#ifdef USE_CLUSTER_BUFFERS

	// See if this buffer in memory
	for (U32 Index = 0; Index < VD_MAX_BUFFERS; Index++)
	{
		if (Clusters[Index].Cluster == Cluster && (Clusters[Index].Flags & VD_CB_VALID))
		{
			memcpy(Buffer, Clusters[Index].Buffer, VD_CLUSTER_SIZE);
			Clusters[Index].AccessRead++;
			return 1;
		}
	}

	Index = GetOldestBuffer();

	if ((Clusters[Index].Flags & VD_CB_VALID) && (Clusters[Index].Flags & VD_CB_MODIFIED))
	{
		if (FlushCluster(Clusters[Index].Cluster, Clusters[Index].Buffer) == 0) return 0;
		Clusters[Index].Flags &= (~VD_CB_VALID);
		Clusters[Index].Flags &= (~VD_CB_MODIFIED);
	}

	U32 Physical = Cluster * VD_CLUSTER_SIZE;

	DWORD NewPosition = ::SetFilePointer(hFile, Physical, NULL, FILE_BEGIN);

	if (NewPosition == 0xFFFFFFFF) return 0;

	DWORD BytesRead;

	BOOL Ret = ::ReadFile
	(
		hFile,
		(LPVOID) Clusters[Index].Buffer,
		VD_CLUSTER_SIZE,
		&BytesRead,
		NULL
	);

	if (BytesRead != VD_CLUSTER_SIZE) return 0;

	Clusters[Index].Flags		|= VD_CB_VALID;
	Clusters[Index].Flags		&= (~VD_CB_MODIFIED);
	Clusters[Index].Cluster		= Cluster;
	Clusters[Index].AccessRead	= 1;
	Clusters[Index].AccessWrite	= 0;

	memcpy(Buffer, Clusters[Index].Buffer, VD_CLUSTER_SIZE);

#else

	U32 Physical = Cluster * VD_CLUSTER_SIZE;

	DWORD NewPosition = ::SetFilePointer(hFile, Physical, NULL, FILE_BEGIN);

	if (NewPosition == 0xFFFFFFFF) return 0;

	DWORD BytesRead;

	BOOL Ret = ::ReadFile
	(
		hFile,
		(LPVOID) Buffer,
		VD_CLUSTER_SIZE,
		&BytesRead,
		NULL
	);

	if (BytesRead != VD_CLUSTER_SIZE) return 0;

#endif

	return 1;
}

/*************************************************************************************************/

int VirtualDrive :: WriteCluster (U32 Cluster, U8* Buffer)
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;

#ifdef USE_CLUSTER_BUFFERS

	for (U32 Index = 0; Index < VD_MAX_BUFFERS; Index++)
	{
		if ((Clusters[Index].Flags & VD_CB_VALID) && Clusters[Index].Cluster == Cluster)
		{
			memcpy(Clusters[Index].Buffer, Buffer, VD_CLUSTER_SIZE);

			Clusters[Index].Flags		|= VD_CB_MODIFIED;
			Clusters[Index].AccessWrite++;

			return 1;
		}

		if ((Clusters[Index].Flags & VD_CB_VALID) == 0)
		{
			memcpy(Clusters[Index].Buffer, Buffer, VD_CLUSTER_SIZE);

			Clusters[Index].Flags		|= VD_CB_VALID;
			Clusters[Index].Flags		|= VD_CB_MODIFIED;
			Clusters[Index].Cluster		= Cluster;
			Clusters[Index].AccessRead	= 0;
			Clusters[Index].AccessWrite	= 1;

			return 1;
		}
	}

	Index = GetOldestBuffer();

	if ((Clusters[Index].Flags & VD_CB_VALID) && (Clusters[Index].Flags & VD_CB_MODIFIED))
	{
		if (FlushCluster(Clusters[Index].Cluster, Clusters[Index].Buffer) == 0) return 0;
		Clusters[Index].Flags &= (~VD_CB_VALID);
		Clusters[Index].Flags &= (~VD_CB_MODIFIED);
	}

	memcpy(Clusters[Index].Buffer, Buffer, VD_CLUSTER_SIZE);

	Clusters[Index].Flags		|= VD_CB_VALID;
	Clusters[Index].Flags		|= VD_CB_MODIFIED;
	Clusters[Index].Cluster		= Cluster;
	Clusters[Index].AccessRead	= 0;
	Clusters[Index].AccessWrite	= 1;

#else

	U32 Physical = Cluster * VD_CLUSTER_SIZE;

	DWORD NewPosition = ::SetFilePointer(hFile, Physical, NULL, FILE_BEGIN);

	if (NewPosition == 0xFFFFFFFF) return 0;

	DWORD BytesWritten;

	BOOL Ret = ::WriteFile
	(
		hFile,
		(LPCVOID) Buffer,
		VD_CLUSTER_SIZE,
		&BytesWritten,
		NULL
	);

	if (BytesWritten != VD_CLUSTER_SIZE) return 0;

	::FlushFileBuffers(hFile);

#endif

	return 1;
}

/*************************************************************************************************/

int VirtualDrive :: FlushCluster (U32 Cluster, U8* Buffer)
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	U32 Physical = Cluster * VD_CLUSTER_SIZE;

	DWORD NewPosition = ::SetFilePointer(hFile, Physical, NULL, FILE_BEGIN);

	if (NewPosition == 0xFFFFFFFF) return 0;

	DWORD BytesWritten;

	BOOL Ret = ::WriteFile
	(
		hFile,
		(LPCVOID) Buffer,
		VD_CLUSTER_SIZE,
		&BytesWritten,
		NULL
	);

	if (BytesWritten != VD_CLUSTER_SIZE) return 0;

	// ::FlushFileBuffers(hFile);

	return 1;
}

/*************************************************************************************************/

VD_FILELOC VirtualDrive :: LocateFileRecord (const String& Path)
{
	VD_FILELOC FileLoc = { 0, 0, 0 };

	//---------------------------------

	if (hFile == INVALID_HANDLE_VALUE) return FileLoc;
	if (Path.Size() == 0) return FileLoc;

	//---------------------------------

	// Allocate locals
	U32				MainCluster		= 0;
	U32				Cluster			= 0;
	U32				Offset			= 0;
	U32				Index			= 0;
	U32				PathSize		= Path.Size();
	LPVD_FILERECORD	FileRecord		= NULL;
	U8				Buffer [VD_CLUSTER_SIZE];

	//---------------------------------

	FileRecord = (LPVD_FILERECORD) Buffer;

	// Is this the root ?
	if (Path == String("/"))
	{
		FileLoc.MainCluster	= SuperBlock.Root;
		FileLoc.Cluster		= SuperBlock.Root;
		FileLoc.Offset		= 0;

		return FileLoc;
	}

	// Is this a full path from the root ?
	if (Path[0] == '/')
	{
		if (Path[1] != '/')
		{
			// Error : Path cannot begin with single '/'
			return FileLoc;
		}

		Cluster = SuperBlock.Root;
		Index += 2;
		if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
	}
	else
	{
		// From current dir. (implement later)
		return FileLoc;
	}

	//---------------------------------

	// Here we are supposed to have a FileRecord describing a folder
	MainCluster	= FileRecord->ClusterTable;
	Cluster		= MainCluster;
	if (Cluster == 0) return FileLoc;

	// Read the folder ClusterTable
	if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
	FileRecord = (LPVD_FILERECORD) Buffer;

	//---------------------------------

	while (1)
	{
		String Component;

		// Parse the next component to look for
		while (Path[Index] != '\0')
		{
			if (Path[Index] == '/') { Index++; break; }
			else Component += String(Path[Index++]);
		}

		//---------------------------------

		// if (Component.Empty() && Index == PathSize) return 1;

		while (1)
		{
			if (stricmp(Component(), FileRecord->Name) == 0)
			{
				if (Index == PathSize)
				{
					FileLoc.MainCluster	= MainCluster;
					FileLoc.Cluster		= Cluster;
					FileLoc.Offset		= Offset;

					return FileLoc;
				}

				if ((FileRecord->Attributes & VD_ATTR_FOLDER) == 0) return FileLoc;

				// Go to FileRecord entries of this file
				MainCluster = FileRecord->ClusterTable;
				Cluster		= MainCluster;
				if (Cluster == 0) return FileLoc;

				if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
				FileRecord	= (LPVD_FILERECORD) Buffer;
				Offset		= 0;

				break;	// Go to next path component
			}
			else
			{
				if (FileRecord->Sibling == VD_FR_END)
				{
					return FileLoc;
				}
				else
				if (FileRecord->Sibling == VD_FR_NEXTVALID)
				{
					FileRecord++;
					Offset++;
				}
				else
				{
					Cluster = FileRecord->Sibling;
					if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
					FileRecord	= (LPVD_FILERECORD) Buffer;
					Offset		= 0;
				}
			}
		}
	}

	return FileLoc;
}

/*************************************************************************************************/

VD_FILELOC VirtualDrive :: GetNewFileRecord (U32 FolderCluster)
{
	VD_FILELOC FileLoc = { 0, 0, 0 };

	if (hFile == NULL) return FileLoc;
	if (FolderCluster == 0) return FileLoc;

	//---------------------------------

	U32	MainCluster	= FolderCluster;
	U32	Cluster		= FolderCluster;
	U32	Offset		= 0;

	U8	Buffer [VD_CLUSTER_SIZE];

	//---------------------------------

	if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
	LPVD_FILERECORD FileRecord = (LPVD_FILERECORD) Buffer;

	//---------------------------------

	while (1)
	{
		if (FileRecord[Offset].Attributes & VD_ATTR_FREE)
		{
			U32 Sibling = FileRecord[Offset].Sibling;
			InitFileRecord(FileRecord + Offset);
			FileRecord[Offset].Sibling = Sibling;

			FileLoc.MainCluster	= MainCluster;
			FileLoc.Cluster		= Cluster;
			FileLoc.Offset		= Offset;

			return FileLoc;
		}
		else
		if (FileRecord[Offset].Sibling == VD_FR_END)
		{
			// Do we have to resize the FileRecord table ?
			if (Offset == (VD_MAX_RECORDINCLUSTER - 1))
			{
				U32 NewCluster = CreateNewCluster();
				if (NewCluster == 0) return FileLoc;

				FileRecord[Offset].Sibling = NewCluster;
				if (WriteCluster(Cluster, Buffer) == 0) return FileLoc;

				if (ReadCluster(NewCluster, Buffer) == 0) return FileLoc;
				FileRecord = (LPVD_FILERECORD) Buffer;
				InitFileRecord(FileRecord);
				if (WriteCluster(NewCluster, Buffer) == 0) return FileLoc;

				FileLoc.MainCluster	= MainCluster;
				FileLoc.Cluster		= NewCluster;
				FileLoc.Offset		= 0;

				return FileLoc;
			}
			else
			{
				FileRecord[Offset].Sibling = VD_FR_NEXTVALID; Offset++;
				InitFileRecord(FileRecord + Offset);
				if (WriteCluster(Cluster, Buffer) == 0) return FileLoc;

				FileLoc.MainCluster	= MainCluster;
				FileLoc.Cluster		= Cluster;
				FileLoc.Offset		= Offset;

				return FileLoc;
			}
		}
		else
		if (FileRecord[Offset].Sibling == VD_FR_NEXTVALID)
		{
			Offset++;
		}
		else
		{
			Cluster = FileRecord[Offset].Sibling;
			if (ReadCluster(Cluster, Buffer) == 0) return FileLoc;
			Offset = 0;
		}
	}

	return FileLoc;
}

/*************************************************************************************************/

U32 VirtualDrive :: FindFreeCluster ()
{
	U8	Buffer [VD_CLUSTER_SIZE];

	// Try to find a free cluster in the bitmap
	register U32	Cluster			= 0;
	register U32	BitmapCluster	= SuperBlock.ClusterBitmap;
	register U32	BitmapOffset	= 0;
	register U32	BitmapBit		= 0;
	register U32	Bit				= 0;

	if (ReadCluster(BitmapCluster, Buffer) == 0) return 0;

	while (1)
	{
		Bit = ((Buffer[BitmapOffset] >> BitmapBit) & 0x00000001UL);

		if (Bit == 0) return Cluster;

		Cluster++; BitmapBit++;
		if (BitmapBit == 8)
		{
			BitmapOffset++; BitmapBit = 0;
			if (BitmapOffset == VD_CLUSTER_SIZE)
			{
				// Implement later
				break;
			}
		}

		if (Cluster == SuperBlock.NumClusters) return 0;
	}

	return 0;
}

/*************************************************************************************************/

U32 VirtualDrive :: CreateNewCluster ()
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	U32 NewCluster = FindFreeCluster();

	if (NewCluster != 0)
	{
		if (MarkCluster(NewCluster, 1) == 0) return 0;
		return NewCluster;
	}

	NewCluster = SuperBlock.NumClusters;

#ifdef USE_CLUSTER_BUFFERS

	for (U32 Index = 0; Index < VD_MAX_BUFFERS; Index++)
	{
		if ((Clusters[Index].Flags & VD_CB_VALID) == 0)
		{
			memset(Clusters[Index].Buffer, 0, VD_CLUSTER_SIZE);

			Clusters[Index].Flags		|= VD_CB_VALID;
			Clusters[Index].Flags		|= VD_CB_MODIFIED;
			Clusters[Index].Cluster		= NewCluster;
			Clusters[Index].AccessRead	= 0;
			Clusters[Index].AccessWrite	= 1;

			if (MarkCluster(NewCluster, 1) == 0) return 0;

			SuperBlock.NumClusters++;
			return NewCluster;
		}
	}

	Index = GetOldestBuffer();

	if ((Clusters[Index].Flags & VD_CB_VALID) && (Clusters[Index].Flags & VD_CB_MODIFIED))
	{
		if (FlushCluster(Clusters[Index].Cluster, Clusters[Index].Buffer) == 0) return 0;
		Clusters[Index].Flags &= (~VD_CB_VALID);
		Clusters[Index].Flags &= (~VD_CB_MODIFIED);
	}

	memset(Clusters[Index].Buffer, 0, VD_CLUSTER_SIZE);

	Clusters[Index].Flags		|= VD_CB_VALID;
	Clusters[Index].Flags		|= VD_CB_MODIFIED;
	Clusters[Index].Cluster		= NewCluster;
	Clusters[Index].AccessRead	= 0;
	Clusters[Index].AccessWrite	= 1;

	if (MarkCluster(NewCluster, 1) == 0) return 0;

	SuperBlock.NumClusters++;
	return NewCluster;

#else

	U32 Physical = NewCluster * VD_CLUSTER_SIZE;

	DWORD NewPosition = ::SetFilePointer(hFile, Physical, NULL, FILE_BEGIN);

	if (NewPosition == 0xFFFFFFFF) return 0;

	U8		Buffer [VD_CLUSTER_SIZE];
	DWORD	BytesWritten;

	CLEAR_CLUSTER(Buffer);

	BOOL Ret = ::WriteFile
	(
		hFile,
		(LPCVOID) Buffer,
		VD_CLUSTER_SIZE,
		&BytesWritten,
		NULL
	);

	if (BytesWritten != VD_CLUSTER_SIZE) return 0;

	::FlushFileBuffers(hFile);

	MarkCluster(NewCluster, 1);

	SuperBlock.NumClusters++;
	return NewCluster;

#endif
}

/*************************************************************************************************/

U32 VirtualDrive :: GetNewFileHandle ()
{
	if (hFile == INVALID_HANDLE_VALUE) return 0xFFFFFFFF;

	for (U32 c = 1; c < VD_MAX_FILES; c++)
	{
		if ((FileStruct[c].Flags & VD_FS_USED) == 0)
		{
			memset(FileStruct + c, 0, sizeof (VD_FILESTRUCT));
			return c;
		}
	}

	return 0xFFFFFFFF;
}

/*************************************************************************************************/

int VirtualDrive :: InitFileRecord (LPVD_FILERECORD FileRecord)
{
	FileRecord->Size			= 0;
	FileRecord->SizeReserved	= 0;
	FileRecord->Attributes		= 0;
	FileRecord->Time_Creation	= GetCurrentTime();
	FileRecord->Time_Accessed	= FileRecord->Time_Creation;
	FileRecord->Time_Modified	= FileRecord->Time_Creation;
	FileRecord->ClusterTable	= 0;
	FileRecord->Sibling			= VD_FR_END;

	memset(FileRecord->Name, 0, sizeof (FileRecord->Name));

	return 1;
}

/*************************************************************************************************/

int VirtualDrive :: ClearFileClusters (VD_FILELOC Loc, int Purge)
{
	U8 Buffer [VD_CLUSTER_SIZE];

	// Get the file's FileRecord
	if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
	LPVD_FILERECORD FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

	// Get the file's ClusterTable
	U32 ClusterTable = FileRecord->ClusterTable;
	if (ClusterTable == 0) return 0;

	// Read the file's ClusterTable
	if (ReadCluster(ClusterTable, Buffer) == 0) return 0;

	U32* ClusterEntry      = (U32*) Buffer;
	U32  ClusterEntryIndex = 0;

	while (1)
	{
		if (ClusterEntry[ClusterEntryIndex] == VD_CT_END)
		{
			// Mark the current cluster table as invalid
			if (MarkCluster(ClusterTable, 0) == 0) return 0;

			return 1;
		}
		else
		if (ClusterEntryIndex == (VD_MAX_ENTRYINCLUSTER - 1))
		{
			// Mark the current cluster table as invalid
			if (MarkCluster(ClusterTable, 0) == 0) return 0;

			// Write back this part of the ClusterTable
			// if (WriteCluster(ClusterTable, Buffer) == 0) return 0;

			// Read the next part of the ClusterTable
			ClusterTable = ClusterEntry[ClusterEntryIndex];
			if (ReadCluster(ClusterTable, Buffer) == 0) return 0;

			// Go to first entry
			ClusterEntryIndex = 0;
		}
		else
		{
			// Purge the current data cluster if requested
			if (Purge)
			{
				CLEAR_CLUSTER(IOBuffer);
				if (WriteCluster(ClusterEntry[ClusterEntryIndex], IOBuffer) == 0) return 0;
			}

			// Mark the current data cluster as free
			if (MarkCluster(ClusterEntry[ClusterEntryIndex], 0) == 0) return 0;

			// Proceed to next entry
			ClusterEntryIndex++;
		}
	}

	return 1;
}

/*************************************************************************************************/

U32 VirtualDrive :: ComputeFileSize (VD_FILELOC Loc)
{
	U8 Buffer [VD_CLUSTER_SIZE];

	// Get the file's FileRecord
	if (ReadCluster(Loc.Cluster, Buffer) == 0) return 0;
	LPVD_FILERECORD FileRecord = ((LPVD_FILERECORD) Buffer) + Loc.Offset;

	// Get the file's ClusterTable
	U32 ClusterTable = FileRecord->ClusterTable;
	if (ClusterTable == 0) return 0;

	// Read the file's ClusterTable
	if (ReadCluster(ClusterTable, Buffer) == 0) return 0;

	U32*	ClusterEntry		= (U32*) Buffer;
	U32		ClusterEntryIndex	= 0;

	U32 FileSize = 0;

	while (1)
	{
		if (ClusterEntry[ClusterEntryIndex] == VD_CT_END) break;
		else
		if (ClusterEntryIndex == (VD_MAX_ENTRYINCLUSTER - 1))
		{
			ClusterTable = ClusterEntry[ClusterEntryIndex];
			if (ReadCluster(ClusterTable, Buffer) == 0) return 0;
			ClusterEntryIndex = 0;
		}
		else
		{
			FileSize += VD_CLUSTER_SIZE;
			ClusterEntryIndex++;
		}
	}

	return FileSize;
}

/*************************************************************************************************/

VD_FILETIME VirtualDrive :: GetCurrentTime ()
{
	VD_FILETIME	FileTime;
	SYSTEMTIME	SystemTime;

	GetSystemTime(&SystemTime);

	FileTime.Year		= SystemTime.wYear;
	FileTime.Month		= SystemTime.wMonth;
	FileTime.Day		= SystemTime.wDay;
	FileTime.Seconds	= ((U32) SystemTime.wHour) * ((U32) SystemTime.wMinute) * ((U32) SystemTime.wSecond);
	FileTime.Reserved	= 0;

	return FileTime;
}

/*************************************************************************************************/

U32 VirtualDrive :: GetOldestBuffer ()
{
	U32 BestRead	= 0xFFFFFFFF;
	U32 BestWrite	= 0xFFFFFFFF;
	U32	ReadCount	= 0xFFFFFFFF;
	U32	WriteCount	= 0xFFFFFFFF;
	U32	Index		= 0;

	for (Index = 0; Index < VD_MAX_BUFFERS; Index++)
	{
		if (Clusters[Index].Flags & VD_CB_VALID)
		{
			if (Clusters[Index].AccessRead < ReadCount)
			{
				BestRead	= Index;
				ReadCount	= Clusters[Index].AccessRead;
			}

			if (Clusters[Index].AccessWrite < WriteCount)
			{
				BestWrite	= Index;
				WriteCount	= Clusters[Index].AccessWrite;
			}

			// Clusters[Index].AccessRead	= 0;
			// Clusters[Index].AccessWrite	= 0;
		}
		else return Index;
	}

	if (BestRead != 0xFFFFFFFF && BestWrite != 0xFFFFFFFF)
	{
		if (ReadCount < WriteCount) Index = BestRead; else Index = BestWrite;
	}
	else
	{
		Index = BestWrite;
		if (Index == 0xFFFFFFFF) Index = BestRead;
		if (Index == 0xFFFFFFFF) Index = 0;
	}

	/*
	U32 BestRead	= 0;
	U32 BestWrite	= 0;
	U32	ReadCount	= 0;
	U32	WriteCount	= 0;
	U32	Index		= 0;

	for (Index = 0; Index < VD_MAX_BUFFERS; Index++)
	{
		if (Clusters[Index].Flags & VD_CB_VALID)
		{
			if (Clusters[Index].AccessRead > ReadCount)
			{
				BestRead	= Index;
				ReadCount	= Clusters[Index].AccessRead;
			}
			if (Clusters[Index].AccessWrite > WriteCount)
			{
				BestWrite	= Index;
				WriteCount	= Clusters[Index].AccessWrite;
			}
		}
		else return Index;
	}

	if (ReadCount > WriteCount) Index = BestRead; else Index = BestWrite;
	*/

	return Index;
}

/*************************************************************************************************/

int VirtualDrive :: IsDriveValid ()
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;
	if (SuperBlock.Magic != VD_MAGIC) return 0;

	return 1;
}

/*************************************************************************************************/

int VirtualDrive :: IsFileHandleValid (U32 Handle)
{
	if (hFile == INVALID_HANDLE_VALUE) return 0;
	if (Handle == 0) return 0;
	if (Handle >= VD_MAX_FILES) return 0;
	if ((FileStruct[Handle].Flags & VD_FS_USED)	== 0) return 0;

	return 1;
}

/*************************************************************************************************/

int VirtualDrive :: MarkCluster (U32 Target, U32 Used)
{
	U32	MaxBitmappedClusters = (SuperBlock.ClusterBitmapSize * VD_CLUSTER_SIZE) * 8;
	if (Target >= MaxBitmappedClusters) return 0;

	U8	Buffer [VD_CLUSTER_SIZE];

	U32	Byte	=	Target / 8;
	U32	Bit		=	Target % 8;
	U32	Cluster	=	SuperBlock.ClusterBitmap + (Byte / VD_CLUSTER_SIZE);
	U32	Offset	=	Byte % VD_CLUSTER_SIZE;
	U32	Value	=	0x00000001UL << Bit;

	if (ReadCluster(Cluster, Buffer) == 0) return 0;

	if (Used) Buffer[Offset] |= Value; else Buffer[Offset] &= (~Value);

	if (WriteCluster(Cluster, Buffer) == 0) return 0;

	return 1;
}

/*************************************************************************************************/

U32 VirtualDrive :: GetClusterMark (U32 Target)
{
	U32	MaxBitmappedClusters = (SuperBlock.ClusterBitmapSize * VD_CLUSTER_SIZE) * 8;
	if (Target >= MaxBitmappedClusters) return 0;

	U8	Buffer [VD_CLUSTER_SIZE];

	U32	Byte	=	Target / 8;
	U32	Bit		=	Target % 8;
	U32	Cluster	=	SuperBlock.ClusterBitmap + (Byte / VD_CLUSTER_SIZE);
	//U32	Offset	=	SuperBlock.ClusterBitmap + (Byte % VD_CLUSTER_SIZE);
	U32	Offset	=	Byte % VD_CLUSTER_SIZE;

	if (ReadCluster(Cluster, Buffer) == 0) return 0;

	return ((Buffer[Offset] >> Bit) & 0x00000001UL);
}

/*************************************************************************************************/

// Implementation of class : VDStream

IMPLEMENT_CLASS (VDStream, Object)

/*************************************************************************************************/

ECCAPI VDStream :: VDStream (VirtualDrive& SourceDrive, const String& FileName, StreamMode Mode)
: Drive(SourceDrive)
{
	hFile = 0;

	Open(FileName, Mode);
}

/*************************************************************************************************/

ECCAPI VDStream :: VDStream (const VDStream& Target) : Drive(Target.Drive)
{
	*this = Target;
}

/*************************************************************************************************/

ECCAPI VDStream :: ~VDStream ()
{
	if (hFile) Drive.CloseFile(hFile);
}

/*************************************************************************************************/

ECCAPI VDStream& VDStream :: operator = (const VDStream& Target)
{
	Object :: operator = (Target);

	return *this;
}

/*************************************************************************************************/

ECCAPI int VDStream :: Open (const String& FileName, StreamMode Mode)
{
	if (hFile != 0) Close();

	U32	Access		= 0;
	U32	Creation	= 0;

	if (Mode & In)
	{
		Access		|= VD_READ;
		Creation	= VD_OPEN_EXISTING;
	}

	if (Mode & Out)
	{
		Access		|= VD_WRITE;
		Creation	= VD_CREATE_ALWAYS;
	}

	if (Mode & Append)
	{
		Access		|= VD_APPEND;
	}

	hFile = Drive.OpenFile(FileName(), Access, Creation);

	if (hFile == 0)
	{
		SetState((StreamState)(GetState() | BadBit | FailBit));
		return 0;
	}

	return 1;
}

/*************************************************************************************************/

ECCAPI int VDStream :: Close ()
{
	if (hFile)
	{
		Drive.CloseFile(hFile);
		hFile = 0;
	}

	return 1;
}

/*************************************************************************************************/

ECCAPI StreamPos VDStream :: GetPosition () const
{
	if (hFile)
	{
		return Drive.GetFilePointer(hFile);
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI StreamPos VDStream :: SetPosition (StreamPos NewPosition, StreamSeek SeekMode)
{
	if (hFile)
	{
		U32 VDSeekMode = 0;

		switch (SeekMode)
		{
			case Start		: VDSeekMode = VD_BEGIN;	break;
			case End		: VDSeekMode = VD_END;		break;
			case Current	: VDSeekMode = VD_CURRENT;	break;
			default			: VDSeekMode = VD_BEGIN;	break;
		}

		U32 Result = Drive.SetFilePointer(hFile, NewPosition, VDSeekMode);
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI StreamPos VDStream :: GetSize () const
{
	if (hFile)
	{
		return Drive.GetFileSize(hFile);
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI U32 VDStream :: ReadBuffer (void* Buffer, U32 Size)
{
	if (hFile)
	{
		U32 BytesRead = Drive.ReadFile(hFile, Buffer, Size);

		if (BytesRead != Size)
		{
			SetState((StreamState) (GetState() | FailBit | EofBit));
		}

		return BytesRead;
	}

	return 0;
}

/*************************************************************************************************/

ECCAPI U32 VDStream :: WriteBuffer (const void* Buffer, U32 Size)
{
	if (hFile)
	{
		U32 BytesWritten = Drive.WriteFile(hFile, Buffer, Size);

		if (BytesWritten != Size)
		{
			SetState((StreamState) (GetState() | BadBit | FailBit));
		}

		return BytesWritten;
	}

	return 0;
}

/*************************************************************************************************/
