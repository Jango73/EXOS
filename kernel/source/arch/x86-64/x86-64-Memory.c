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


    x86-64 memory helpers

\************************************************************************/

#include "arch/x86-64/x86-64-Memory.h"

#include "Kernel.h"

/************************************************************************/

/**
 * @brief Compute the size of the 4 KiB page window reserved in low memory.
 * @param TotalMemoryBytes Total detected physical memory size in bytes.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL ComputeLowMemoryWindowLimit(UINT TotalMemoryBytes) {
    if (TotalMemoryBytes == 0) {
        return 0;
    }

    UINT OnePercent = TotalMemoryBytes / 100u;
    if ((TotalMemoryBytes % 100u) != 0u) {
        OnePercent++;
    }

    UINT Window = (OnePercent + (PAGE_2M_SIZE - 1u)) & ~(PAGE_2M_MASK);

    if (Window > TotalMemoryBytes) {
        Window = TotalMemoryBytes;
    }

    return (PHYSICAL)Window;
}

/************************************************************************/

/**
 * @brief Convenience wrapper to compute the low window limit from startup info.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL GetLowMemoryWindowLimit(void) {
    return ComputeLowMemoryWindowLimit(KernelStartup.MemorySize);
}

/************************************************************************/

