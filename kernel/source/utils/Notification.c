/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Generic Notification System

\************************************************************************/

#include "../include/utils/Notification.h"
#include "../include/Heap.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/List.h"
#include "../include/ID.h"

/************************************************************************/

/**
 * @brief Creates a new notification context.
 * @return Pointer to the new context or NULL on failure.
 */
LPNOTIFICATION_CONTEXT Notification_CreateContext(void) {
    LPNOTIFICATION_CONTEXT Context = (LPNOTIFICATION_CONTEXT)KernelHeapAlloc(sizeof(NOTIFICATION_CONTEXT));
    if (Context) {
        Context->NotificationList = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        if (!Context->NotificationList) {
            KernelHeapFree(Context);
            return NULL;
        }
        DEBUG(TEXT("[Notification_CreateContext] Created context at %x"), (U32)Context);
    }
    return Context;
}

/************************************************************************/

/**
 * @brief Destroys a notification context and frees all entries.
 * @param Context Context to destroy.
 */
void Notification_DestroyContext(LPNOTIFICATION_CONTEXT Context) {
    if (!Context) return;

    DEBUG(TEXT("[Notification_DestroyContext] Destroying context at %x"), (U32)Context);

    if (Context->NotificationList) {
        DeleteList(Context->NotificationList);
    }

    KernelHeapFree(Context);
}

/************************************************************************/

/**
 * @brief Registers a callback for a specific event.
 * @param Context Notification context.
 * @param EventID Event identifier.
 * @param Callback Callback function.
 * @param UserData User data passed to callback.
 * @return 1 on success, 0 on failure.
 */
U32 Notification_Register(LPNOTIFICATION_CONTEXT Context, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    LPNOTIFICATION_ENTRY Entry;

    if (!Context || !Callback) {
        DEBUG(TEXT("[Notification_Register] Invalid parameters: Context=%x Callback=%x"), (U32)Context, (U32)Callback);
        return 0;
    }

    Entry = (LPNOTIFICATION_ENTRY)KernelHeapAlloc(sizeof(NOTIFICATION_ENTRY));
    if (!Entry) {
        DEBUG(TEXT("[Notification_Register] Failed to allocate entry"));
        return 0;
    }

    static U32 NextID = 1;
    Entry->TypeID = NextID++;
    Entry->References = 1;
    Entry->Next = NULL;
    Entry->Prev = NULL;
    Entry->EventID = EventID;
    Entry->Callback = Callback;
    Entry->UserData = UserData;

    if (!ListAddTail(Context->NotificationList, Entry)) {
        DEBUG(TEXT("[Notification_Register] Failed to add entry to list"));
        KernelHeapFree(Entry);
        return 0;
    }

    DEBUG(TEXT("[Notification_Register] Registered callback %x for event %x"), (U32)Callback, EventID);
    return 1;
}

/************************************************************************/

/**
 * @brief Unregisters a callback for a specific event.
 * @param Context Notification context.
 * @param EventID Event identifier.
 * @param Callback Callback function.
 * @param UserData User data passed to callback.
 * @return 1 on success, 0 on failure.
 */
U32 Notification_Unregister(LPNOTIFICATION_CONTEXT Context, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    LPNOTIFICATION_ENTRY Current;
    U32 Index, Size;

    if (!Context || !Callback) {
        DEBUG(TEXT("[Notification_Unregister] Invalid parameters"));
        return 0;
    }

    Size = ListGetSize(Context->NotificationList);
    for (Index = 0; Index < Size; Index++) {
        Current = (LPNOTIFICATION_ENTRY)ListGetItem(Context->NotificationList, Index);
        if (Current && Current->EventID == EventID && Current->Callback == Callback && Current->UserData == UserData) {
            ListErase(Context->NotificationList, Current);
            DEBUG(TEXT("[Notification_Unregister] Unregistered callback %x for event %x"), (U32)Callback, EventID);
            return 1;
        }
    }

    DEBUG(TEXT("[Notification_Unregister] Callback %x for event %x not found"), (U32)Callback, EventID);
    return 0;
}

/************************************************************************/

/**
 * @brief Sends a notification to all registered callbacks.
 * @param Context Notification context.
 * @param EventID Event identifier.
 * @param Data Event data.
 * @param DataSize Size of event data.
 */
void Notification_Send(LPNOTIFICATION_CONTEXT Context, U32 EventID, LPVOID Data, U32 DataSize) {
    LPNOTIFICATION_ENTRY Current;
    NOTIFICATION_DATA NotificationData;
    U32 CallbackCount = 0;
    U32 Index, Size;

    if (!Context) {
        DEBUG(TEXT("[Notification_Send] Invalid context"));
        return;
    }

    NotificationData.EventID = EventID;
    NotificationData.Data = Data;
    NotificationData.DataSize = DataSize;

    DEBUG(TEXT("[Notification_Send] Sending event %x with %u bytes data"), EventID, DataSize);

    Size = ListGetSize(Context->NotificationList);
    for (Index = 0; Index < Size; Index++) {
        Current = (LPNOTIFICATION_ENTRY)ListGetItem(Context->NotificationList, Index);
        if (Current && Current->EventID == EventID) {
            DEBUG(TEXT("[Notification_Send] Calling callback %x for event %x"), (U32)Current->Callback, EventID);
            Current->Callback(&NotificationData, Current->UserData);
            CallbackCount++;
        }
    }

    DEBUG(TEXT("[Notification_Send] Event %x sent to %u callbacks"), EventID, CallbackCount);
}

/************************************************************************/
