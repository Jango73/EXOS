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


    Boot path capabilities

\************************************************************************/

#include "utils/BootPath.h"

#include "vbr-multiboot.h"

/************************************************************************/

static U32 BootPathMultibootFlags = 0;

/************************************************************************/

/**
 * @brief Update cached multiboot flags used by boot capability helpers.
 * @param Flags Multiboot flags value from bootloader information block.
 */
void BootPathSetMultibootFlags(U32 Flags) {
    BootPathMultibootFlags = Flags;
}

/************************************************************************/

/**
 * @brief Retrieve cached multiboot flags.
 * @return Cached multiboot flags.
 */
U32 BootPathGetMultibootFlags(void) {
    return BootPathMultibootFlags;
}

/************************************************************************/

/**
 * @brief Check whether bootloader exported VBE information.
 * @return TRUE when MULTIBOOT_INFO_VBE_INFO is present.
 */
BOOL BootPathHasVbeInfo(void) {
    return (BootPathMultibootFlags & MULTIBOOT_INFO_VBE_INFO) != 0 ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Check whether VESA backend can run on current boot path.
 * @return TRUE when VESA probing and mode switching are allowed.
 */
BOOL VesaIsSupportedOnCurrentBootPath(void) {
#if defined(__EXOS_ARCH_X86_64__)
    return FALSE;
#else
    return BootPathHasVbeInfo();
#endif
}

/************************************************************************/
