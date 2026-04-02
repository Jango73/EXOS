
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

#ifndef MESSAGE_QUEUE_H_INCLUDED
#define MESSAGE_QUEUE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "user/User.h"
#include "process/Message.h"

/************************************************************************/

/************************************************************************/

typedef struct tag_MESSAGE_QUEUE_BUFFER {
    LPMESSAGE Entries;
    UINT Capacity;
    UINT Head;
    UINT Count;
} MESSAGE_QUEUE_BUFFER, *LPMESSAGE_QUEUE_BUFFER;

/************************************************************************/

void MessageQueueBufferInitialize(LPMESSAGE_QUEUE_BUFFER Queue,
                                  LPMESSAGE Storage,
                                  UINT Capacity);
void MessageQueueBufferReset(LPMESSAGE_QUEUE_BUFFER Queue);
BOOL MessageQueueBufferIsInitialized(const MESSAGE_QUEUE_BUFFER* Queue);
UINT MessageQueueBufferGetCount(const MESSAGE_QUEUE_BUFFER* Queue);
BOOL MessageQueueBufferPush(LPMESSAGE_QUEUE_BUFFER Queue, LPCMESSAGE Message);
BOOL MessageQueueBufferPeek(const MESSAGE_QUEUE_BUFFER* Queue, LPMESSAGE Message);
BOOL MessageQueueBufferPop(LPMESSAGE_QUEUE_BUFFER Queue, LPMESSAGE Message);
BOOL MessageQueueBufferReadAt(const MESSAGE_QUEUE_BUFFER* Queue, UINT Offset, LPMESSAGE Message);
BOOL MessageQueueBufferRemoveAt(LPMESSAGE_QUEUE_BUFFER Queue, UINT Offset, LPMESSAGE Message);

/************************************************************************/

#endif  // MESSAGE_QUEUE_H_INCLUDED
