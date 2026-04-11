
/************************************************************************\

    EXOS Test program - Module Host
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


    Module Host - Test program for executable module loading

\************************************************************************/

#include "../../../../runtime/include/exos-runtime.h"
#include "../../../../runtime/include/exos.h"

/***************************************************************************/

typedef UINT (*MODULE_SAMPLE_ADD)(UINT Value);

/***************************************************************************/

int exosmain(int argc, char** argv) {
    HANDLE Module;
    MODULE_SAMPLE_ADD ModuleSampleAdd;
    UINT Result;

    UNUSED(argc);
    UNUSED(argv);

    printf("Module host starting...\n");

    Module = LoadModule("/system/apps/test/module-sample");
    if (Module == 0) {
        printf("Module load failed\n");
        return 1;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbol(Module, "ModuleSampleAdd");
    if (ModuleSampleAdd == NULL) {
        printf("Module symbol lookup failed\n");
        return 1;
    }

    Result = ModuleSampleAdd(0x23);
    printf("Module result: %u\n", Result);

    return (Result == 0x2B) ? 0 : 1;
}
