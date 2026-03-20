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


    Desktop 3D cube component

\************************************************************************/

#include "ui/Cube3D.h"

#include "Clock.h"
#include "CoreString.h"
#include "Heap.h"
#include "math/Math.h"
#include "math/Math3D.h"

/***************************************************************************/

#define CUBE3D_TIMER_ID 1
#define CUBE3D_TIMER_INTERVAL_MS 200
#define CUBE3D_PROP_STATE TEXT("desktop.cube3d.state")
#define CUBE3D_ROTATION_X_MDEG_PER_SECOND 12000
#define CUBE3D_ROTATION_Y_MDEG_PER_SECOND 20000
#define CUBE3D_ROTATION_Z_MDEG_PER_SECOND 7000
#define CUBE3D_FULL_TURN_MDEG 360000
#define CUBE3D_DEFAULT_WIDTH 420
#define CUBE3D_DEFAULT_HEIGHT 300
#define CUBE3D_COLOR_FLASH_GREEN ((COLOR)0x0014FF39)

/***************************************************************************/

typedef struct tag_CUBE3D_STATE {
    HANDLE FlashPen;
    U32 AngleXMilliDegrees;
    U32 AngleYMilliDegrees;
    U32 AngleZMilliDegrees;
    U32 LastTick;
} CUBE3D_STATE, *LPCUBE3D_STATE;

/***************************************************************************/

static const VERTEX3 CubeVertices[8] = {
    {-1.0f, -1.0f, -1.0f},
    {1.0f, -1.0f, -1.0f},
    {1.0f, 1.0f, -1.0f},
    {-1.0f, 1.0f, -1.0f},
    {-1.0f, -1.0f, 1.0f},
    {1.0f, -1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f}};

static const QUAD CubeQuads[6] = {
    {0, 1, 2, 3},
    {4, 5, 6, 7},
    {0, 4, 7, 3},
    {1, 5, 6, 2},
    {3, 2, 6, 7},
    {0, 1, 5, 4}};

/***************************************************************************/

/**
 * @brief Resolve preferred size for the cube window.
 * @param SizeOut Receives preferred size.
 * @return TRUE on success.
 */
BOOL Cube3DGetPreferredSize(LPPOINT SizeOut) {
    if (SizeOut == NULL) return FALSE;

    SizeOut->X = CUBE3D_DEFAULT_WIDTH;
    SizeOut->Y = CUBE3D_DEFAULT_HEIGHT;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure 3D cube window class is registered.
 * @return TRUE on success.
 */
BOOL Cube3DEnsureClassRegistered(void) {
    if (FindWindowClass(DESKTOP_CUBE3D_WINDOW_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(DESKTOP_CUBE3D_WINDOW_CLASS_NAME, 0, NULL, Cube3DWindowFunc, 0) != NULL;
}

/***************************************************************************/

/**
 * @brief Project one transformed 3D point to 2D client coordinates.
 * @param Point3D Input point in view space.
 * @param CenterX Projection center X.
 * @param CenterY Projection center Y.
 * @param Focal Perspective focal factor.
 * @param XOut Projected X.
 * @param YOut Projected Y.
 * @return TRUE when projected point is valid.
 */
static BOOL Cube3DProjectPoint(VECTOR3 Point3D, I32 CenterX, I32 CenterY, F32 Focal, I32* XOut, I32* YOut) {
    F32 InverseZ;

    if (XOut == NULL || YOut == NULL) {
        return FALSE;
    }

    if (Point3D.Z <= 0.1f) {
        return FALSE;
    }

    InverseZ = 1.0f / Point3D.Z;
    *XOut = CenterX + (I32)(Point3D.X * Focal * InverseZ);
    *YOut = CenterY - (I32)(Point3D.Y * Focal * InverseZ);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one line segment for cube wireframe.
 * @param LineInfo Shared line descriptor.
 * @param X1 First point X.
 * @param Y1 First point Y.
 * @param X2 Second point X.
 * @param Y2 Second point Y.
 */
static void Cube3DDrawEdge(LPLINE_INFO LineInfo, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    if (LineInfo == NULL) return;

    LineInfo->X1 = X1;
    LineInfo->Y1 = Y1;
    LineInfo->X2 = X2;
    LineInfo->Y2 = Y2;
    (void)Line(LineInfo);
}

/***************************************************************************/

/**
 * @brief Render one rotating cube in wireframe.
 * @param Window Target window.
 * @param ClientRect Window client rectangle.
 */
static void Cube3DDrawWireframe(HANDLE Window, LPRECT ClientRect) {
    HANDLE GraphicsContext;
    HANDLE PreviousPen;
    LINE_INFO LineInfo;
    MATRIX4 Transform;
    VECTOR3 Euler;
    VECTOR3 Translation;
    VECTOR3 Scale;
    VECTOR3 Transformed[8];
    I32 ScreenX[8];
    I32 ScreenY[8];
    BOOL Valid[8];
    I32 ClientWidth;
    I32 ClientHeight;
    I32 CenterX;
    I32 CenterY;
    F32 Focal;
    UINT VertexIndex;
    UINT QuadIndex;
    LPCUBE3D_STATE State;

    if (Window == NULL || ClientRect == NULL) return;

    GraphicsContext = BeginWindowDraw(Window);
    if (GraphicsContext == NULL) {
        return;
    }

    ClientWidth = ClientRect->X2 - ClientRect->X1 + 1;
    ClientHeight = ClientRect->Y2 - ClientRect->Y1 + 1;
    if (ClientWidth <= 0 || ClientHeight <= 0) {
        (void)EndWindowDraw(Window);
        return;
    }

    CenterX = ClientRect->X1 + (ClientWidth / 2);
    CenterY = ClientRect->Y1 + (ClientHeight / 2);
    Focal = (F32)((ClientWidth < ClientHeight) ? ClientWidth : ClientHeight) * 0.65f;

    State = (LPCUBE3D_STATE)GetWindowProp(Window, CUBE3D_PROP_STATE);
    if (State == NULL || State->FlashPen == NULL) {
        (void)EndWindowDraw(Window);
        return;
    }

    Euler = Math3DVector3(
        ((F32)State->AngleXMilliDegrees * MATH_PI_F32) / 180000.0f,
        ((F32)State->AngleYMilliDegrees * MATH_PI_F32) / 180000.0f,
        ((F32)State->AngleZMilliDegrees * MATH_PI_F32) / 180000.0f);
    Translation = Math3DVector3(0.0f, 0.0f, 5.0f);
    Scale = Math3DVector3(1.35f, 1.35f, 1.35f);
    Transform = Math3DMatrix4ComposeTRS(Translation, Euler, Scale);

    for (VertexIndex = 0; VertexIndex < 8; VertexIndex++) {
        VECTOR3 ModelPoint = Math3DVector3(CubeVertices[VertexIndex].X, CubeVertices[VertexIndex].Y, CubeVertices[VertexIndex].Z);

        Transformed[VertexIndex] = Math3DMatrix4TransformPoint(Transform, ModelPoint);
        Valid[VertexIndex] = Cube3DProjectPoint(Transformed[VertexIndex], CenterX, CenterY, Focal, &ScreenX[VertexIndex], &ScreenY[VertexIndex]);
    }

    (void)SelectBrush(GraphicsContext, NULL);
    PreviousPen = SelectPen(GraphicsContext, State->FlashPen);

    LineInfo.Header.Size = sizeof(LINE_INFO);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GraphicsContext;

    for (QuadIndex = 0; QuadIndex < 6; QuadIndex++) {
        QUAD Face = CubeQuads[QuadIndex];

        if (Valid[Face.A] && Valid[Face.B]) Cube3DDrawEdge(&LineInfo, ScreenX[Face.A], ScreenY[Face.A], ScreenX[Face.B], ScreenY[Face.B]);
        if (Valid[Face.B] && Valid[Face.C]) Cube3DDrawEdge(&LineInfo, ScreenX[Face.B], ScreenY[Face.B], ScreenX[Face.C], ScreenY[Face.C]);
        if (Valid[Face.C] && Valid[Face.D]) Cube3DDrawEdge(&LineInfo, ScreenX[Face.C], ScreenY[Face.C], ScreenX[Face.D], ScreenY[Face.D]);
        if (Valid[Face.D] && Valid[Face.A]) Cube3DDrawEdge(&LineInfo, ScreenX[Face.D], ScreenY[Face.D], ScreenX[Face.A], ScreenY[Face.A]);
    }

    (void)SelectPen(GraphicsContext, PreviousPen);
    (void)EndWindowDraw(Window);
}

/***************************************************************************/

/**
 * @brief 3D cube component window procedure.
 * @param Window Target window handle.
 * @param Message Window message.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 Cube3DWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    U32 LastTick;
    U32 Now;
    U32 DeltaMilliseconds;
    U32 DeltaXMilliDegrees;
    U32 DeltaYMilliDegrees;
    U32 DeltaZMilliDegrees;
    LPCUBE3D_STATE State;
    PEN_INFO PenInfo;

    switch (Message) {
        case EWM_CREATE:
            State = (LPCUBE3D_STATE)HeapAlloc(sizeof(CUBE3D_STATE));
            if (State == NULL) {
                return 0;
            }

            MemorySet(State, 0, sizeof(CUBE3D_STATE));
            PenInfo.Header.Size = sizeof(PEN_INFO);
            PenInfo.Header.Version = EXOS_ABI_VERSION;
            PenInfo.Header.Flags = 0;
            PenInfo.Color = CUBE3D_COLOR_FLASH_GREEN;
            PenInfo.Pattern = MAX_U32;
            PenInfo.Width = 4;
            PenInfo.Flags = 0;
            State->FlashPen = CreatePen(&PenInfo);
            if (State->FlashPen == NULL) {
                HeapFree(State);
                return 0;
            }

            Now = GetSystemTime();
            State->LastTick = Now;
            (void)SetWindowProp(Window, CUBE3D_PROP_STATE, (UINT)State);
            (void)SetWindowTimer(Window, CUBE3D_TIMER_ID, CUBE3D_TIMER_INTERVAL_MS);
            return 1;

        case EWM_DELETE:
            (void)KillWindowTimer(Window, CUBE3D_TIMER_ID);
            State = (LPCUBE3D_STATE)GetWindowProp(Window, CUBE3D_PROP_STATE);
            if (State != NULL) {
                if (State->FlashPen != NULL) {
                    (void)DeleteObject(State->FlashPen);
                }
                HeapFree(State);
                (void)SetWindowProp(Window, CUBE3D_PROP_STATE, 0);
            }
            break;

        case EWM_TIMER:
            if (Param1 == CUBE3D_TIMER_ID) {
                State = (LPCUBE3D_STATE)GetWindowProp(Window, CUBE3D_PROP_STATE);
                if (State == NULL) {
                    return 1;
                }

                Now = GetSystemTime();
                LastTick = State->LastTick;
                DeltaMilliseconds = Now - LastTick;

                if (DeltaMilliseconds == 0) {
                    return 1;
                }

                DeltaXMilliDegrees = (DeltaMilliseconds * CUBE3D_ROTATION_X_MDEG_PER_SECOND) / 1000;
                DeltaYMilliDegrees = (DeltaMilliseconds * CUBE3D_ROTATION_Y_MDEG_PER_SECOND) / 1000;
                DeltaZMilliDegrees = (DeltaMilliseconds * CUBE3D_ROTATION_Z_MDEG_PER_SECOND) / 1000;
                State->AngleXMilliDegrees = (State->AngleXMilliDegrees + DeltaXMilliDegrees) % CUBE3D_FULL_TURN_MDEG;
                State->AngleYMilliDegrees = (State->AngleYMilliDegrees + DeltaYMilliDegrees) % CUBE3D_FULL_TURN_MDEG;
                State->AngleZMilliDegrees = (State->AngleZMilliDegrees + DeltaZMilliDegrees) % CUBE3D_FULL_TURN_MDEG;
                State->LastTick = Now;
                (void)InvalidateClientRect(Window, NULL);
            }
            return 1;

        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);
            if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
                return 1;
            }

            Cube3DDrawWireframe(Window, &ClientRect);
            return 1;

        default:
            break;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
