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


    Desktop pipeline trace

\************************************************************************/

#ifndef DESKTOP_PIPELINE_TRACE_H_INCLUDED
#define DESKTOP_PIPELINE_TRACE_H_INCLUDED

/************************************************************************/

#include "Desktop.h"
#include "utils/RectRegion.h"

/************************************************************************/

void DesktopPipelineTraceRegion(LPWINDOW Window, LPRECT_REGION Region);
void DesktopPipelineTraceWindowDrawDispatch(LPWINDOW Window, LPRECT ClipRect, LPRECT ClientScreenRect);

/************************************************************************/

#endif  // DESKTOP_PIPELINE_TRACE_H_INCLUDED
