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


    Generic docking host behavior implementation

\************************************************************************/

#include "ui/layout/DockHost.h"
#include "CoreString.h"

/************************************************************************/

typedef struct tag_DOCK_EDGE_BUCKET {
    LPDOCKABLE Items[DOCK_HOST_MAX_ITEMS];
    UINT Count;
} DOCK_EDGE_BUCKET, *LPDOCK_EDGE_BUCKET;

/************************************************************************/

/**
 * @brief Check one edge value.
 * @param Edge Candidate edge.
 * @return TRUE when valid.
 */
static BOOL DockHostIsValidEdge(U32 Edge) {
    if (Edge == DOCK_EDGE_NONE) return TRUE;
    if (Edge == DOCK_EDGE_TOP) return TRUE;
    if (Edge == DOCK_EDGE_BOTTOM) return TRUE;
    if (Edge == DOCK_EDGE_LEFT) return TRUE;
    if (Edge == DOCK_EDGE_RIGHT) return TRUE;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Check one overflow policy.
 * @param Policy Candidate policy.
 * @return TRUE when valid.
 */
static BOOL DockHostIsValidOverflowPolicy(U32 Policy) {
    if (Policy == DOCK_OVERFLOW_POLICY_CLIP) return TRUE;
    if (Policy == DOCK_OVERFLOW_POLICY_SHRINK) return TRUE;
    if (Policy == DOCK_OVERFLOW_POLICY_REJECT) return TRUE;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Validate one host rectangle.
 * @param Rect Candidate rectangle.
 * @return TRUE when valid.
 */
static BOOL DockHostIsValidRect(LPRECT Rect) {
    if (Rect == NULL) return FALSE;
    if (Rect->X2 < Rect->X1) return FALSE;
    if (Rect->Y2 < Rect->Y1) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate one edge policy object.
 * @param Policy Candidate policy.
 * @return TRUE when valid.
 */
static BOOL DockHostValidateEdgePolicy(LPDOCK_EDGE_LAYOUT_POLICY Policy) {
    if (Policy == NULL) return FALSE;
    if (Policy->MarginStart < 0) return FALSE;
    if (Policy->MarginEnd < 0) return FALSE;
    if (Policy->Spacing < 0) return FALSE;
    if (DockHostIsValidOverflowPolicy(Policy->OverflowPolicy) == FALSE) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate one host policy object.
 * @param Policy Candidate policy.
 * @return TRUE when valid.
 */
static BOOL DockHostValidatePolicy(LPDOCK_HOST_LAYOUT_POLICY Policy) {
    if (Policy == NULL) return FALSE;
    if (Policy->PaddingTop < 0) return FALSE;
    if (Policy->PaddingBottom < 0) return FALSE;
    if (Policy->PaddingLeft < 0) return FALSE;
    if (Policy->PaddingRight < 0) return FALSE;
    if (DockHostValidateEdgePolicy(&(Policy->Top)) == FALSE) return FALSE;
    if (DockHostValidateEdgePolicy(&(Policy->Bottom)) == FALSE) return FALSE;
    if (DockHostValidateEdgePolicy(&(Policy->Left)) == FALSE) return FALSE;
    if (DockHostValidateEdgePolicy(&(Policy->Right)) == FALSE) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Compare two dockables with deterministic tie-break.
 * @param Left First dockable.
 * @param Right Second dockable.
 * @return Negative when Left should be first.
 */
static I32 DockHostCompareDockables(LPDOCKABLE Left, LPDOCKABLE Right) {
    if (Left->Priority < Right->Priority) return -1;
    if (Left->Priority > Right->Priority) return 1;
    if (Left->Order < Right->Order) return -1;
    if (Left->Order > Right->Order) return 1;
    if (Left->InsertionIndex < Right->InsertionIndex) return -1;
    if (Left->InsertionIndex > Right->InsertionIndex) return 1;
    return 0;
}

/************************************************************************/

/**
 * @brief Stable-sort one edge bucket according to docking order contract.
 * @param Bucket Target bucket.
 */
static void DockHostSortBucket(LPDOCK_EDGE_BUCKET Bucket) {
    UINT I;
    UINT J;
    LPDOCKABLE Temp;

    if (Bucket == NULL) return;

    for (I = 0; I < Bucket->Count; I++) {
        for (J = I + 1; J < Bucket->Count; J++) {
            if (DockHostCompareDockables(Bucket->Items[J], Bucket->Items[I]) < 0) {
                Temp = Bucket->Items[I];
                Bucket->Items[I] = Bucket->Items[J];
                Bucket->Items[J] = Temp;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Build one edge bucket from all attached dockables.
 * @param Host Source host.
 * @param Edge Target edge.
 * @param Bucket Destination bucket.
 */
static void DockHostBuildBucket(LPDOCK_HOST Host, U32 Edge, LPDOCK_EDGE_BUCKET Bucket) {
    UINT Index;
    LPDOCKABLE Dockable;

    Bucket->Count = 0;
    for (Index = 0; Index < Host->ItemCount; Index++) {
        Dockable = Host->Items[Index];
        if (Dockable == NULL) continue;
        if (Dockable->Visible == FALSE) continue;
        if (Dockable->Enabled == FALSE) continue;
        if (Dockable->Edge != Edge) continue;
        if (Bucket->Count >= DOCK_HOST_MAX_ITEMS) break;
        Bucket->Items[Bucket->Count++] = Dockable;
    }

    DockHostSortBucket(Bucket);
}

/************************************************************************/

/**
 * @brief Resolve requested edge thickness for one bucket.
 * @param Bucket Edge bucket.
 * @return Thickness in pixels.
 */
static I32 DockHostResolveEdgeThickness(LPDOCK_EDGE_BUCKET Bucket) {
    UINT Index;
    DOCK_SIZE_REQUEST Request;
    I32 Thickness = 0;
    I32 Candidate;

    if (Bucket == NULL || Bucket->Count == 0) return 0;

    for (Index = 0; Index < Bucket->Count; Index++) {
        Request = Bucket->Items[Index]->SizeRequest;
        Candidate = Request.PreferredPrimarySize;
        if (Candidate < Request.MinimumPrimarySize) Candidate = Request.MinimumPrimarySize;
        if (Request.MaximumPrimarySize > 0 && Candidate > Request.MaximumPrimarySize) {
            Candidate = Request.MaximumPrimarySize;
        }
        if (Candidate > Thickness) Thickness = Candidate;
    }

    if (Thickness <= 0) Thickness = 1;
    return Thickness;
}

/************************************************************************/

/**
 * @brief Fill one layout result object with host baseline values.
 * @param Host Source host.
 * @param Result Destination layout result.
 */
static void DockHostInitializeLayoutResult(LPDOCK_HOST Host, LPDOCK_LAYOUT_RESULT Result) {
    Result->Status = DOCK_LAYOUT_STATUS_SUCCESS;
    Result->HostRect = Host->HostRect;
    Result->WorkRect = Host->WorkRect;
    Result->DockableCount = Host->ItemCount;
    Result->AppliedCount = 0;
    Result->RejectedCount = 0;
}

/************************************************************************/

/**
 * @brief Set default host policy values.
 * @param Host Target host.
 */
static void DockHostSetDefaultPolicy(LPDOCK_HOST Host) {
    Host->Policy.PaddingTop = 0;
    Host->Policy.PaddingBottom = 0;
    Host->Policy.PaddingLeft = 0;
    Host->Policy.PaddingRight = 0;

    Host->Policy.Top.MarginStart = 0;
    Host->Policy.Top.MarginEnd = 0;
    Host->Policy.Top.Spacing = 0;
    Host->Policy.Top.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Bottom.MarginStart = 0;
    Host->Policy.Bottom.MarginEnd = 0;
    Host->Policy.Bottom.Spacing = 0;
    Host->Policy.Bottom.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Left.MarginStart = 0;
    Host->Policy.Left.MarginEnd = 0;
    Host->Policy.Left.Spacing = 0;
    Host->Policy.Left.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Right.MarginStart = 0;
    Host->Policy.Right.MarginEnd = 0;
    Host->Policy.Right.Spacing = 0;
    Host->Policy.Right.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;
}

/************************************************************************/

/**
 * @brief Return one edge policy by edge value.
 * @param Host Host owning policy.
 * @param Edge Target edge.
 * @return Edge policy pointer or NULL.
 */
static LPDOCK_EDGE_LAYOUT_POLICY DockHostGetEdgePolicy(LPDOCK_HOST Host, U32 Edge) {
    if (Edge == DOCK_EDGE_TOP) return &(Host->Policy.Top);
    if (Edge == DOCK_EDGE_BOTTOM) return &(Host->Policy.Bottom);
    if (Edge == DOCK_EDGE_LEFT) return &(Host->Policy.Left);
    if (Edge == DOCK_EDGE_RIGHT) return &(Host->Policy.Right);
    return NULL;
}

/************************************************************************/

/**
 * @brief Apply one edge bucket in one side-by-side band.
 * @param Host Target host.
 * @param Edge Edge value.
 * @param Bucket Sorted edge bucket.
 * @param WorkRect In-out work rectangle.
 * @param Result In-out layout result.
 * @return Docking status code.
 */
static U32 DockHostApplyEdgeBucket(
    LPDOCK_HOST Host,
    U32 Edge,
    LPDOCK_EDGE_BUCKET Bucket,
    LPRECT WorkRect,
    LPDOCK_LAYOUT_RESULT Result
) {
    LPDOCK_EDGE_LAYOUT_POLICY EdgePolicy;
    I32 Thickness;
    I32 PrimaryStart;
    I32 PrimaryEnd;
    I32 PrimaryAvailable;
    I32 Cursor;
    I32 Segment;
    I32 RemainingItems;
    I32 ItemStart;
    I32 ItemEnd;
    RECT AssignedRect;
    UINT Index;
    U32 Status;

    if (Bucket->Count == 0) return DOCK_LAYOUT_STATUS_SUCCESS;

    EdgePolicy = DockHostGetEdgePolicy(Host, Edge);
    if (EdgePolicy == NULL) return DOCK_LAYOUT_STATUS_INVALID_EDGE;

    Thickness = DockHostResolveEdgeThickness(Bucket);
    if (Edge == DOCK_EDGE_TOP || Edge == DOCK_EDGE_BOTTOM) {
        if ((WorkRect->Y2 - WorkRect->Y1 + 1) < Thickness) {
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) {
                Result->RejectedCount += Bucket->Count;
                return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
            }
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_SHRINK) {
                Thickness = WorkRect->Y2 - WorkRect->Y1 + 1;
            }
        }

        PrimaryStart = WorkRect->X1 + EdgePolicy->MarginStart;
        PrimaryEnd = WorkRect->X2 - EdgePolicy->MarginEnd;
    } else {
        if ((WorkRect->X2 - WorkRect->X1 + 1) < Thickness) {
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) {
                Result->RejectedCount += Bucket->Count;
                return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
            }
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_SHRINK) {
                Thickness = WorkRect->X2 - WorkRect->X1 + 1;
            }
        }

        PrimaryStart = WorkRect->Y1 + EdgePolicy->MarginStart;
        PrimaryEnd = WorkRect->Y2 - EdgePolicy->MarginEnd;
    }

    if (PrimaryEnd < PrimaryStart) {
        Result->RejectedCount += Bucket->Count;
        return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
    }

    PrimaryAvailable = PrimaryEnd - PrimaryStart + 1;
    Cursor = PrimaryStart;

    for (Index = 0; Index < Bucket->Count; Index++) {
        RemainingItems = (I32)Bucket->Count - (I32)Index;
        Segment = (PrimaryEnd - Cursor + 1) / RemainingItems;
        if (Segment < 1) Segment = 1;

        ItemStart = Cursor;
        ItemEnd = ItemStart + Segment - 1;
        if (Index + 1 == Bucket->Count) ItemEnd = PrimaryEnd;

        if (Edge == DOCK_EDGE_TOP) {
            AssignedRect.X1 = ItemStart;
            AssignedRect.X2 = ItemEnd;
            AssignedRect.Y1 = WorkRect->Y1;
            AssignedRect.Y2 = WorkRect->Y1 + Thickness - 1;
        } else if (Edge == DOCK_EDGE_BOTTOM) {
            AssignedRect.X1 = ItemStart;
            AssignedRect.X2 = ItemEnd;
            AssignedRect.Y2 = WorkRect->Y2;
            AssignedRect.Y1 = WorkRect->Y2 - Thickness + 1;
        } else if (Edge == DOCK_EDGE_LEFT) {
            AssignedRect.Y1 = ItemStart;
            AssignedRect.Y2 = ItemEnd;
            AssignedRect.X1 = WorkRect->X1;
            AssignedRect.X2 = WorkRect->X1 + Thickness - 1;
        } else {
            AssignedRect.Y1 = ItemStart;
            AssignedRect.Y2 = ItemEnd;
            AssignedRect.X2 = WorkRect->X2;
            AssignedRect.X1 = WorkRect->X2 - Thickness + 1;
        }

        if (Bucket->Items[Index]->Callbacks.ApplyRect != NULL) {
            Status = Bucket->Items[Index]->Callbacks.ApplyRect(
                Bucket->Items[Index],
                Host,
                &AssignedRect,
                WorkRect
            );
            if (Status != DOCK_LAYOUT_STATUS_SUCCESS) {
                Result->RejectedCount++;
                Result->Status = DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
                if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) return Status;
            } else {
                Result->AppliedCount++;
            }
        } else {
            Result->AppliedCount++;
        }

        Cursor = ItemEnd + 1 + EdgePolicy->Spacing;
    }

    if (Edge == DOCK_EDGE_TOP) {
        WorkRect->Y1 += Thickness;
    } else if (Edge == DOCK_EDGE_BOTTOM) {
        WorkRect->Y2 -= Thickness;
    } else if (Edge == DOCK_EDGE_LEFT) {
        WorkRect->X1 += Thickness;
    } else {
        WorkRect->X2 -= Thickness;
    }

    if (WorkRect->X2 < WorkRect->X1) WorkRect->X2 = WorkRect->X1;
    if (WorkRect->Y2 < WorkRect->Y1) WorkRect->Y2 = WorkRect->Y1;

    UNUSED(PrimaryAvailable);
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

BOOL DockHostInit(LPDOCK_HOST Host, LPCSTR Identifier, LPVOID Context) {
    if (Host == NULL || Identifier == NULL) return FALSE;

    Host->Identifier = Identifier;
    Host->Context = Context;
    Host->HostRect.X1 = 0;
    Host->HostRect.Y1 = 0;
    Host->HostRect.X2 = 0;
    Host->HostRect.Y2 = 0;
    Host->WorkRect = Host->HostRect;
    Host->LayoutSequence = 0;
    Host->LayoutDirty = TRUE;
    Host->ItemCount = 0;
    Host->Capacity = DOCK_HOST_MAX_ITEMS;

    DockHostSetDefaultPolicy(Host);
    return TRUE;
}

/************************************************************************/

BOOL DockHostReset(LPDOCK_HOST Host) {
    if (Host == NULL) return FALSE;
    Host->ItemCount = 0;
    Host->WorkRect = Host->HostRect;
    Host->LayoutDirty = TRUE;
    return TRUE;
}

/************************************************************************/

U32 DockHostSetHostRect(LPDOCK_HOST Host, LPRECT HostRect) {
    if (Host == NULL || HostRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidRect(HostRect) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Host->HostRect = *HostRect;
    Host->WorkRect = *HostRect;
    Host->LayoutDirty = TRUE;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

U32 DockHostSetPolicy(LPDOCK_HOST Host, LPDOCK_HOST_LAYOUT_POLICY Policy) {
    if (Host == NULL || Policy == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostValidatePolicy(Policy) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_POLICY;

    Host->Policy = *Policy;
    Host->LayoutDirty = TRUE;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

U32 DockHostAttachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable) {
    UINT Index;

    if (Host == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidEdge(Dockable->Edge) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_EDGE;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] == Dockable) return DOCK_LAYOUT_STATUS_ALREADY_ATTACHED;
        if (Host->Items[Index] == NULL) continue;
        if (Host->Items[Index]->Identifier == NULL || Dockable->Identifier == NULL) continue;
        if (StringCompare(Host->Items[Index]->Identifier, Dockable->Identifier) == 0) {
            return DOCK_LAYOUT_STATUS_DUPLICATE_IDENTIFIER;
        }
    }

    if (Host->ItemCount >= Host->Capacity || Host->ItemCount >= DOCK_HOST_MAX_ITEMS) {
        return DOCK_LAYOUT_STATUS_CAPACITY_EXCEEDED;
    }

    Dockable->InsertionIndex = ++Host->LayoutSequence;
    Host->Items[Host->ItemCount++] = Dockable;
    Host->LayoutDirty = TRUE;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

U32 DockHostDetachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable) {
    UINT Index;
    UINT MoveIndex;

    if (Host == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] != Dockable) continue;
        for (MoveIndex = Index; MoveIndex + 1 < Host->ItemCount; MoveIndex++) {
            Host->Items[MoveIndex] = Host->Items[MoveIndex + 1];
        }
        Host->Items[Host->ItemCount - 1] = NULL;
        Host->ItemCount--;
        Host->LayoutDirty = TRUE;
        return DOCK_LAYOUT_STATUS_SUCCESS;
    }

    return DOCK_LAYOUT_STATUS_NOT_ATTACHED;
}

/************************************************************************/

U32 DockHostRelayout(LPDOCK_HOST Host, LPDOCK_LAYOUT_RESULT Result) {
    DOCK_EDGE_BUCKET Bucket;
    RECT WorkRect;
    U32 Status;
    UINT Index;

    if (Host == NULL || Result == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidRect(&(Host->HostRect)) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    WorkRect = Host->HostRect;
    WorkRect.X1 += Host->Policy.PaddingLeft;
    WorkRect.Y1 += Host->Policy.PaddingTop;
    WorkRect.X2 -= Host->Policy.PaddingRight;
    WorkRect.Y2 -= Host->Policy.PaddingBottom;
    if (WorkRect.X2 < WorkRect.X1 || WorkRect.Y2 < WorkRect.Y1) {
        return DOCK_LAYOUT_STATUS_CONSTRAINT_VIOLATION;
    }

    Host->WorkRect = WorkRect;
    DockHostInitializeLayoutResult(Host, Result);

    DockHostBuildBucket(Host, DOCK_EDGE_TOP, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_TOP, &Bucket, &WorkRect, Result);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Result->Status == DOCK_LAYOUT_STATUS_SUCCESS) Result->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_BOTTOM, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_BOTTOM, &Bucket, &WorkRect, Result);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Result->Status == DOCK_LAYOUT_STATUS_SUCCESS) Result->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_LEFT, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_LEFT, &Bucket, &WorkRect, Result);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Result->Status == DOCK_LAYOUT_STATUS_SUCCESS) Result->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_RIGHT, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_RIGHT, &Bucket, &WorkRect, Result);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Result->Status == DOCK_LAYOUT_STATUS_SUCCESS) Result->Status = Status;

    Host->WorkRect = WorkRect;
    Result->WorkRect = WorkRect;
    Host->LayoutDirty = FALSE;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] == NULL) continue;
        if (Host->Items[Index]->Callbacks.OnHostWorkRectChanged == NULL) continue;
        (void)Host->Items[Index]->Callbacks.OnHostWorkRectChanged(
            Host->Items[Index],
            Host,
            &(Host->WorkRect)
        );
    }

    return Result->Status;
}

/************************************************************************/

U32 DockHostGetWorkRect(LPDOCK_HOST Host, LPRECT WorkRect) {
    if (Host == NULL || WorkRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    *WorkRect = Host->WorkRect;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/
