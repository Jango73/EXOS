/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Fixed-size message queue using caller-owned storage.

\************************************************************************/

#include "utils/MessageQueue.h"

/************************************************************************/

static UINT MessageQueueBufferIndexFromOffset(const MESSAGE_QUEUE_BUFFER* Queue,
                                              UINT Offset) {
    return (Queue->Head + Offset) % Queue->Capacity;
}

/************************************************************************/

void MessageQueueBufferInitialize(LPMESSAGE_QUEUE_BUFFER Queue,
                                  LPMESSAGE Storage,
                                  UINT Capacity) {
    if (Queue == NULL || Storage == NULL || Capacity == 0) {
        return;
    }

    Queue->Entries = Storage;
    Queue->Capacity = Capacity;
    Queue->Head = 0;
    Queue->Count = 0;
}

/************************************************************************/

void MessageQueueBufferReset(LPMESSAGE_QUEUE_BUFFER Queue) {
    if (Queue == NULL) {
        return;
    }

    Queue->Head = 0;
    Queue->Count = 0;
}

/************************************************************************/

BOOL MessageQueueBufferIsInitialized(const MESSAGE_QUEUE_BUFFER* Queue) {
    if (Queue == NULL) {
        return FALSE;
    }

    return Queue->Entries != NULL && Queue->Capacity > 0;
}

/************************************************************************/

UINT MessageQueueBufferGetCount(const MESSAGE_QUEUE_BUFFER* Queue) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE) {
        return 0;
    }

    return Queue->Count;
}

/************************************************************************/

BOOL MessageQueueBufferPush(LPMESSAGE_QUEUE_BUFFER Queue, LPCMESSAGE Message) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE || Message == NULL) {
        return FALSE;
    }

    if (Queue->Count >= Queue->Capacity) {
        return FALSE;
    }

    UINT TailOffset = Queue->Count;
    UINT Index = MessageQueueBufferIndexFromOffset(Queue, TailOffset);

    Queue->Entries[Index] = *Message;
    Queue->Count++;

    return TRUE;
}

/************************************************************************/

BOOL MessageQueueBufferPeek(const MESSAGE_QUEUE_BUFFER* Queue, LPMESSAGE Message) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE || Message == NULL || Queue->Count == 0) {
        return FALSE;
    }

    *Message = Queue->Entries[Queue->Head];
    return TRUE;
}

/************************************************************************/

BOOL MessageQueueBufferPop(LPMESSAGE_QUEUE_BUFFER Queue, LPMESSAGE Message) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE || Queue->Count == 0) {
        return FALSE;
    }

    if (Message != NULL) {
        *Message = Queue->Entries[Queue->Head];
    }

    Queue->Head = (Queue->Head + 1) % Queue->Capacity;
    Queue->Count--;

    if (Queue->Count == 0) {
        Queue->Head = 0;
    }

    return TRUE;
}

/************************************************************************/

BOOL MessageQueueBufferReadAt(const MESSAGE_QUEUE_BUFFER* Queue, UINT Offset, LPMESSAGE Message) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE || Message == NULL) {
        return FALSE;
    }

    if (Offset >= Queue->Count) {
        return FALSE;
    }

    UINT Index = MessageQueueBufferIndexFromOffset(Queue, Offset);
    *Message = Queue->Entries[Index];
    return TRUE;
}

/************************************************************************/

BOOL MessageQueueBufferRemoveAt(LPMESSAGE_QUEUE_BUFFER Queue, UINT Offset, LPMESSAGE Message) {
    if (MessageQueueBufferIsInitialized(Queue) == FALSE || Offset >= Queue->Count) {
        return FALSE;
    }

    UINT RemoveIndex = MessageQueueBufferIndexFromOffset(Queue, Offset);

    if (Message != NULL) {
        *Message = Queue->Entries[RemoveIndex];
    }

    UINT Position = Offset;
    while (Position + 1 < Queue->Count) {
        UINT SourceIndex = MessageQueueBufferIndexFromOffset(Queue, Position + 1);
        UINT DestinationIndex = MessageQueueBufferIndexFromOffset(Queue, Position);
        Queue->Entries[DestinationIndex] = Queue->Entries[SourceIndex];
        Position++;
    }

    Queue->Count--;

    if (Queue->Count == 0) {
        Queue->Head = 0;
    }

    return TRUE;
}

/************************************************************************/
