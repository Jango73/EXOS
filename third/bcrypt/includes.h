/* ====================================================================
 * Copyright (c) 2002 Johnny Shelley.  All rights reserved.
 *
 * Bcrypt is licensed under the BSD software license. See the file
 * called 'LICENSE' that you should have received with this software
 * for details
 * ====================================================================
 */

#include "../include/stdlib.h"
#include "../include/stdio.h"
#include "../include/string.h"

#ifndef WIN32   /* These libraries don't exist on Win32 */
#include "../include/unistd.h"
#include "../include/termios.h"
#include "../include/sys/time.h"
#endif

#include "../include/sys/types.h"
#include "../include/sys/stat.h"
#include "../include/zlib.h"

