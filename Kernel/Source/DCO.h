
// DCO.H

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

  Dynamic Class Object

\***************************************************************************/

#ifndef DCO_H_INCLUDED
#define DCO_H_INCLUDED

/***************************************************************************/

#pragma pack (1)

/***************************************************************************/

// #define DCOFUNCTIONTYPE __stdcall
#define DCOFUNCTIONTYPE
#define INTERFACE        struct
#define CONST_FUNCTABLE  const

/***************************************************************************/

typedef BOOL (*DCO_ENUMFUNC) (LPVOID);

/***************************************************************************/

typedef struct tag_UID
{
  U32 Data1;
  U32 Data2;
  U32 Data3;
  U32 Data4;
} UID, *LPUID;

/***************************************************************************/

/*
typedef struct tag_DCO_INTERFACE
{
  DCO_GETINTERFACE GetInterface;
  DCO_ADDREFERENCE AddReference;
  DCO_RELEASE      Release;
} DCO_INTERFACE, *LPDCO_INTERFACE;
*/

/***************************************************************************/

#define BEGIN_INTERFACE
#define END_INTERFACE

/***************************************************************************/

#define DECLARE_INTERFACE(_interface_,_baseinterface_) \
typedef struct tag_##_interface_ \
{ \
  FuncTableOf##_interface_* FuncTablePointer; \
} _interface_, *lp_interface_; \
typedef const struct FuncTableOf##_interface_ FuncTableOf##_interface_;

/***************************************************************************/

extern const UID UID_IUnknown;
extern const UID UID_IObject;
extern const UID UID_ISemaphore;
extern const UID UID_IStream;
extern const UID UID_IPersistStream;
extern const UID UID_IWindow;
extern const UID UID_IFolder;

/***************************************************************************/

typedef INTERFACE IUnknown       IUnknown;
typedef INTERFACE IObject        IObject;
typedef INTERFACE ISemaphore     ISemaphore;
typedef INTERFACE IMemory        IMemory;
typedef INTERFACE IStream        IStream;
typedef INTERFACE IPersistStream IPersistStream;
typedef INTERFACE IWindow        IWindow;
typedef INTERFACE IFolder        IFolder;
typedef INTERFACE IFile          IFile;

/***************************************************************************/

// IObject Interface

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIObject
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IObject*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IObject*);
  U32 (DCOFUNCTIONTYPE *Release) (IObject*);

  END_INTERFACE

};

INTERFACE IObject
{
  CONST_FUNCTABLE struct FuncTableOfIObject* FuncTablePointer;
};

#define IObject_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IObject_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IObject_Release(This) \
(This)->FuncTablePointer->Release(This)

#endif

/***************************************************************************/

// ISemaphore Interface

#if defined(__cplusplus)

#else

typedef struct FuncTableOfISemaphore
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (ISemaphore*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (ISemaphore*);
  U32 (DCOFUNCTIONTYPE *Release) (ISemaphore*);
  U32 (DCOFUNCTIONTYPE *Lock) (ISemaphore*);
  U32 (DCOFUNCTIONTYPE *Unlock) (ISemaphore*);
  U32 (DCOFUNCTIONTYPE *GetLockCount) (ISemaphore*,U32*);

  END_INTERFACE

};

INTERFACE ISemaphore
{
  CONST_FUNCTABLE struct FuncTableOfISemaphore* FuncTablePointer;
};

#define ISemaphore_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define ISemaphore_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define ISemaphore_Release(This) \
(This)->FuncTablePointer->Release(This)

#define ISemaphore_Lock(This) \
(This)->FuncTablePointer->Lock(This)

#define ISemaphore_Unlock(This) \
(This)->FuncTablePointer->Unlock(This)

#define ISemaphore_GetLockCount(This,lpCount) \
(This)->FuncTablePointer->GetLockCount(This,lpCount)

#endif

/***************************************************************************/

// IMemory Interface

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIMemory
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IMemory*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IMemory*);
  U32 (DCOFUNCTIONTYPE *Release) (IMemory*);
  U32 (DCOFUNCTIONTYPE *Alloc) (IMemory*, U32, void**);
  U32 (DCOFUNCTIONTYPE *Realloc) (IMemory*, U32, void**);
  U32 (DCOFUNCTIONTYPE *Free) (IMemory*, void*);

  END_INTERFACE

};

INTERFACE IMemory
{
  CONST_FUNCTABLE struct FuncTableOfIMemory* FuncTablePointer;
};

#define IMemory_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IMemory_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IMemory_Release(This) \
(This)->FuncTablePointer->Release(This)

#define IMemory_Alloc(This,Size,Pointer) \
(This)->FuncTablePointer->Alloc(This,Size,Pointer)

#define IMemory_Realloc(This,Size,Pointer) \
(This)->FuncTablePointer->Realloc(This,Size,Pointer)

#define IMemory_Free(This,Pointer) \
(This)->FuncTablePointer->Free(This,Pointer)

#endif

/***************************************************************************/

// IStream

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIStream
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IStream*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IStream*);
  U32 (DCOFUNCTIONTYPE *Release) (IStream*);
  U32 (DCOFUNCTIONTYPE *Read) (IStream*, void*, U32, U32*);
  U32 (DCOFUNCTIONTYPE *Write) (IStream*, const void*, U32, U32*);
  U32 (DCOFUNCTIONTYPE *Seek) (IStream*, U32, U32, U32*);

  END_INTERFACE

};

INTERFACE IStream
{
  CONST_FUNCTABLE struct FuncTableOfIStream* FuncTablePointer;
};

#define IStream_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IStream_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IStream_Release(This) \
(This)->FuncTablePointer->Release(This)

#define IStream_Read(This,lpBuffer,Count,BytesRead) \
(This)->FuncTablePointer->Read(This,lpBuffer,Count,BytesRead)

#define IStream_Write(This,lpBuffer,Count,BytesWritten) \
(This)->FuncTablePointer->Write(This,lpBuffer,Count,BytesWritten)

#define IStream_Seek(This,Count,Method,NewPos) \
(This)->FuncTablePointer->Seek(This,Count,Method,NewPos)

#endif

/***************************************************************************/

// IPersistStream

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIPersistStream
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IPersistStream*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IPersistStream*);
  U32 (DCOFUNCTIONTYPE *Release) (IPersistStream*);
  U32 (DCOFUNCTIONTYPE *IsDirty) (IPersistStream*);
  U32 (DCOFUNCTIONTYPE *Load) (IPersistStream*, IStream*);
  U32 (DCOFUNCTIONTYPE *Save) (IPersistStream*, IStream*, BOOL);

  END_INTERFACE

};

INTERFACE IPersistStream
{
  CONST_FUNCTABLE struct FuncTableOfIPersistStream* FuncTablePointer;
};

#define IPersistStream_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IPersistStream_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IPersistStream_Release(This) \
(This)->FuncTablePointer->Release(This)

#define IPersistStream_IsDirty(This) \
(This)->FuncTablePointer->IsDirty(This)

#define IPersistStream_Load(This,lpStream) \
(This)->FuncTablePointer->IsDirty(This,lpStream)

#define IPersistStream_Save(This,lpStream,Clear) \
(This)->FuncTablePointer->IsDirty(This,lpStream,Clear)

#endif

/***************************************************************************/

// IWindow

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIWindow
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IWindow*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IWindow*);
  U32 (DCOFUNCTIONTYPE *Release) (IWindow*);

  END_INTERFACE

};

INTERFACE IWindow
{
  CONST_FUNCTABLE struct FuncTableOfIWindow* FuncTablePointer;
};

#define IWindow_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IWindow_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IWindow_Release(This) \
(This)->FuncTablePointer->Release(This)

#endif

/***************************************************************************/

// IFolder

#if defined(__cplusplus)

#else

typedef struct FuncTableOfIFolder
{

  BEGIN_INTERFACE

  U32 (DCOFUNCTIONTYPE *GetInterface) (IFolder*, LPUID, void**);
  U32 (DCOFUNCTIONTYPE *AddReference) (IFolder*);
  U32 (DCOFUNCTIONTYPE *Release) (IFolder*);
  U32 (DCOFUNCTIONTYPE *EnumItems) (IFolder*, DCO_ENUMFUNC*);
  U32 (DCOFUNCTIONTYPE *GetParent) (IFolder*);

  END_INTERFACE

};

INTERFACE IFolder
{
  CONST_FUNCTABLE struct FuncTableOfIFolder* FuncTablePointer;
};

#define IFolder_GetInterface(This,UID,lplpObject) \
(This)->FuncTablePointer->GetInterface(This,UID,lplpObject)

#define IFolder_AddReference(This) \
(This)->FuncTablePointer->GetInterface(This)

#define IFolder_Release(This) \
(This)->FuncTablePointer->Release(This)

#define IFolder_IsDirty(This) \
(This)->FuncTablePointer->IsDirty(This)

#define IFolder_Load(This,lpStream) \
(This)->FuncTablePointer->IsDirty(This,lpStream)

#define IFolder_Save(This,lpStream,Clear) \
(This)->FuncTablePointer->IsDirty(This,lpStream,Clear)

#endif

/***************************************************************************/

// Global DCO Functions

extern BOOL  DCO_Initialize   ();
extern BOOL  DCO_Terminate    ();
extern void* DCO_CreateObject (LPUID);

/***************************************************************************/

#endif
