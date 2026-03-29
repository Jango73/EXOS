# Windowing Layout Components Architecture

## Prerequisite

Add one new window style: `EWS_NO_DRAW`.

Purpose:
- allow creation of windows that exist only as structural layout containers
- keep these windows fully outside the rendering pipeline
- avoid background clear, client drawing, border drawing, and any other visual work for these containers

Expected behavior:
- the window still exists in the normal window tree
- the window still participates in geometry, parenting, clipping ancestry, message routing, and child ownership
- the window is ignored by rendering traversal and does not generate drawing work for itself

This style is required so non-visual layout containers can be implemented as ordinary EXOS windows without paying a rendering cost or introducing fake empty draw handlers.

## Goal

Define one reusable layout subsystem for EXOS window components.

It provides non-visual containers and sizing conventions shared by all UI components that want automatic layout.
- each component can expose its own size needs
- containers measure their children
- the computed size can propagate upward
- the parent later assigns the final rectangle downward

## Design principles

### Independent from window decoration and painting

Layout components are about geometry only.

They do not impose:
- colors
- borders
- backgrounds
- fonts
- drawing behavior

A layout container may be implemented as:
- one real child window with no visual appearance
- or one internal component hosted by a window class

The geometry contract remains the same in both cases.

### Two-pass layout

Layout must always use two distinct passes:

1. Measure pass
   The tree asks each component what size it wants under given constraints.
2. Arrange pass
   The parent assigns the final rectangle to each child.

This is the only robust way to support:
- fixed size on one axis
- automatic size on one axis
- content-driven size
- fill or stretch behavior
- nested containers

### Axis-specific behavior

Width and height must be handled independently.

Each axis may be:
- fixed
- content-sized
- minimum-constrained
- maximum-constrained
- weighted
- unconstrained

A component may therefore express:
- fixed width, automatic height
- automatic width, fixed height
- content width, fill height
- minimum width, weighted height

### Homogeneous conventions

Every layout-aware component must obey the same contract, regardless of its visual role.

Examples:
- label
- button
- custom drawing component
- row container
- grid container
- spacer

## Component family

### Base concepts

#### `LAYOUT_NODE`

Common geometry contract implemented by every layout-aware component.

Responsibilities:
- hold visibility state
- hold layout flags and size policies
- expose measure callback
- expose arrange callback
- expose invalidation state
- point to parent layout node when part of a layout tree

This is not a visual class. It is the base geometry object.

#### `LAYOUT_CONTAINER`

Base type for nodes that own child layout nodes.

Responsibilities:
- append/remove child nodes
- trigger measure invalidation on structural changes
- aggregate children measurements
- dispatch arrangement to children

This is also non-visual by default.

### Non-visual containers

#### `RowLayout`

Horizontal flow, one row.

Behavior:
- places children left to right
- supports spacing
- supports per-child vertical alignment
- can distribute extra width to weighted children
- computes height from the tallest effective child

#### `ColumnLayout`

Vertical flow, one column.

Behavior:
- places children top to bottom
- supports spacing
- supports per-child horizontal alignment
- can distribute extra height to weighted children
- computes width from the widest effective child

#### `GridLayout`

Grid with rows and columns.

Behavior:
- each child belongs to one cell
- row span and column span are supported
- row and column minimum sizes are aggregated from children
- row and column weights distribute extra space
- spacing and outer padding are supported

#### `StackLayout`

Single visible child at a time, all children share the same assigned rectangle.

Useful for:
- tabs
- pages
- wizard steps
- overlays

#### `OverlayLayout`

Children are arranged on top of each other.

Useful for:
- badges
- floating buttons
- status layers
- debug overlays

The main child may fill all space while other children align to corners or edges.

### Utility nodes

#### `Spacer`

Non-visual elastic or fixed gap.

Supports:
- fixed width or height
- minimum size
- weighted expansion on one axis
- zero preferred size with expansion weight

Examples:
- push a group to the right in a row
- reserve constant vertical separation

#### `SeparatorSpace`

Non-visual structural gap owned by the layout, useful when spacing must behave as a child item instead of a global container spacing.

#### `SizeBox`

Wrapper node that overrides child sizing constraints.

Useful for:
- force fixed width
- clamp min/max width
- clamp min/max height
- preserve child auto-sizing on the other axis

This avoids special-casing size hacks inside visual widgets.

#### `AlignmentBox`

Wrapper node that keeps one child at its measured size and aligns it inside the assigned rectangle.

Useful for:
- center one auto-sized component in a larger area
- right-align one fixed-size button


## Size model

### Per-axis policy

Each node exposes one `LAYOUT_AXIS_POLICY` for width and one for height.

Proposed policy values:
- `LAYOUT_SIZE_FIXED`
- `LAYOUT_SIZE_CONTENT`
- `LAYOUT_SIZE_MIN_CONTENT`
- `LAYOUT_SIZE_FILL`
- `LAYOUT_SIZE_WEIGHTED`

Meaning:

- `FIXED`
  The node asks for one exact size on that axis.
- `CONTENT`
  The node computes the preferred size from its content.
- `MIN_CONTENT`
  The node computes its minimum useful size from content, but accepts growth.
- `FILL`
  The node has no intrinsic preferred size and accepts the available space.
- `WEIGHTED`
  The node participates in extra space distribution using one weight.

### Size fields

Every node should expose one axis descriptor like:

- `MinimumSize`
- `PreferredSize`
- `MaximumSize`
- `Weight`
- `Policy`

These values exist independently for width and height.

Maximum size `0` may mean "unbounded".

### Measured size result

The measure pass must return a structured result, not a single pair of integers.

Proposed result:

- minimum width
- minimum height
- preferred width
- preferred height
- maximum width
- maximum height
- baseline support flag
- baseline value when relevant

Baseline is optional but useful for future text alignment in rows and forms.


## Geometry contract

### Input constraints

Measure is performed under explicit constraints.

Proposed constraint object:

- minimum width
- minimum height
- maximum width
- maximum height
- known width flag
- known height flag

This allows cases such as:
- width known, height unknown
- width unconstrained, height capped
- both unconstrained
- both fixed by parent

### Measure contract

Every node implements something equivalent to:

`Measure(Node, Constraints, ResultOut)`

The result must answer:
- what is the minimum useful size
- what is the preferred size
- what size range is acceptable

Examples:

- label:
  width may depend on text measurement
  height may depend on line wrapping if width is constrained
- image:
  width and height may depend on natural bitmap size
- list:
  preferred height may depend on visible item policy
- custom drawing component:
  width may be fixed, height may be content-driven

### Arrange contract

Every node implements something equivalent to:

`Arrange(Node, AssignedRect)`

Responsibilities:
- store final rectangle
- move or resize the backing window if there is one
- propagate arrangement to children if the node is a container

Arrange does not negotiate. It consumes the final rectangle.


## Parent propagation

### Invalidation model

Layout must propagate upward when one child changes a geometry-relevant property.

Examples:
- text changed
- font changed
- visibility changed
- fixed size changed
- child appended or removed
- row or column weight changed

Proposed invalidation flags:
- `LAYOUT_INVALID_MEASURE`
- `LAYOUT_INVALID_ARRANGE`
- `LAYOUT_INVALID_VISUAL`

Only measure invalidation must propagate automatically to layout parents.

### Preferred-size propagation

When a child invalidates its measure:

1. the child marks itself dirty
2. the nearest layout parent is notified
3. the parent invalidates its own measured cache
4. this propagation continues upward
5. the first owning window schedules one layout pass

This solves the required case:
- one component does not know its final size in advance
- it measures itself from content
- the parent recomputes
- that new preferred size can affect grandparent layout

### Window boundary propagation

At one window boundary, there are two valid behaviors:

1. Internal relayout only
   The window keeps its outer rectangle and only rearranges its internal tree.
2. Preferred-size propagation to parent window
   The window updates its own preferred size hint so the parent layout can relayout.

This second mode is essential for child windows used as reusable composite widgets.


## Proposed EXOS integration

### Window properties

The layout subsystem should integrate with the existing window system through explicit properties and callbacks, not by replacing the window model.

Each window class may optionally expose one layout node root.

Proposed per-window optional properties:
- `layout.enabled`
- `layout.role`
- `layout.parent_link_mode`
- `layout.invalidate_pending`

These properties remain metadata. The real geometry state lives in layout structures.

### New class helper layer

Add one helper family parallel to current UI helpers:

- `WindowLayoutNode`
- `WindowLayoutContainer`
- `WindowLayoutHost`

Purpose:
- bind a window to one `LAYOUT_NODE`
- translate window messages into measure and arrange invalidation
- provide one common layout lifecycle for all layout-aware window classes

### Message integration

The layout host should react to:
- `EWM_CREATE`
- `EWM_DELETE`
- `EWM_NOTIFY`
- `EWN_WINDOW_RECT_CHANGED`
- child append and remove events
- property changed events

Typical flow:

1. one child invalidates measure
2. host window posts one deferred layout message to itself
3. host rebuilds measure and arrange from the root layout node
4. children windows are moved only once per cycle

Deferred relayout is important to avoid cascades of immediate nested `MoveWindow()` calls.

### Desktop placement compatibility

This subsystem does not replace the EXOS window tree.

It only manages child geometry inside one window or inside one subtree of child windows.

Desktop placement, z-order, clipping, focus, and message ownership remain unchanged.


## Robust sizing behavior

### Fixed on one axis, automatic on the other

This must be a first-class scenario, not an edge case.

Examples:
- text field:
  fixed height, width fills parent
- label:
  width from content, height from content or wrapping
- toolbar button:
  width from content plus padding, fixed height
- preview pane:
  fixed width, height fills parent

The model supports this because width and height policies are independent.

### Children that compute their own width and/or height

Visual components must be allowed to provide their own measurement logic.

Examples:
- label computes text size
- image computes bitmap size
- list computes visible rows
- custom graph computes minimum readable width

Containers must not guess these values. They query them through `Measure`.

### Containers that must shrink

When the available rectangle is smaller than preferred content:
- minimum sizes are respected first
- weighted and fill items absorb shrink before fixed items when possible
- overflow policy is explicit per container

Proposed container overflow policies:
- clip
- shrink below preferred but not below minimum
- reject arrangement

### Visibility changes

Invisible layout nodes must not contribute to size aggregation unless explicitly configured otherwise.

This is necessary for:
- hidden pages
- optional toolbar sections
- collapsible panels


## Detailed container rules

### `RowLayout`

Per child fields:
- left and right margin
- vertical alignment
- width policy
- height policy
- stretch weight

Measure:
- width = sum of visible children widths + spacing + margins
- height = max of visible children heights

Arrange:
- fixed and content-width children get their measured width first
- remaining width is distributed to fill or weighted children
- each child height is resolved from the assigned row height and its vertical policy

### `ColumnLayout`

Per child fields:
- top and bottom margin
- horizontal alignment
- width policy
- height policy
- stretch weight

Measure:
- width = max of visible children widths
- height = sum of visible children heights + spacing + margins

Arrange:
- fixed and content-height children get their measured height first
- remaining height is distributed to fill or weighted children
- each child width is resolved from the assigned column width and its horizontal policy

### `GridLayout`

Per child fields:
- row
- column
- row span
- column span
- alignment

Measure:
- compute row minimum/preferred heights
- compute column minimum/preferred widths
- merge spanning items conservatively

Arrange:
- resolve final row heights and column widths from available rectangle
- assign each child the cell union for its span
- optional inner alignment if the child does not fill the full cell


## Suggested internal API

### Core structures

Suggested public headers:

- `kernel/include/ui/LayoutTypes.h`
- `kernel/include/ui/LayoutNode.h`
- `kernel/include/ui/LayoutContainer.h`
- `kernel/include/ui/LayoutRow.h`
- `kernel/include/ui/LayoutColumn.h`
- `kernel/include/ui/LayoutGrid.h`
- `kernel/include/ui/LayoutSpacer.h`
- `kernel/include/ui/LayoutSizeBox.h`
- `kernel/include/ui/WindowLayoutHost.h`

Suggested source files:

- `kernel/source/ui/LayoutNode.c`
- `kernel/source/ui/LayoutContainer.c`
- `kernel/source/ui/LayoutMeasure.c`
- `kernel/source/ui/LayoutArrange.c`
- `kernel/source/ui/LayoutRow.c`
- `kernel/source/ui/LayoutColumn.c`
- `kernel/source/ui/LayoutGrid.c`
- `kernel/source/ui/LayoutSpacer.c`
- `kernel/source/ui/LayoutSizeBox.c`
- `kernel/source/ui/WindowLayoutHost.c`

### Core operations

Suggested operations:

- `LayoutNodeInit`
- `LayoutNodeInvalidateMeasure`
- `LayoutNodeInvalidateArrange`
- `LayoutNodeMeasure`
- `LayoutNodeArrange`
- `LayoutContainerAppendChild`
- `LayoutContainerRemoveChild`
- `LayoutHostScheduleRelayout`
- `LayoutHostRunRelayout`


## Example usage patterns

### Dialog footer

Tree:
- `ColumnLayout`
- content area
- `RowLayout`
- spacer
- `Cancel` button
- `Ok` button

Behavior:
- content area grows
- buttons stay content-sized
- spacer absorbs extra width

### Tool panel

Tree:
- `ColumnLayout`
- title
- `GridLayout` with labels and editors
- spacer
- action buttons

Behavior:
- labels keep content width
- editors fill horizontally
- bottom spacer pushes actions downward

### Composite widget with propagated preferred size

Tree:
- window class hosting one `ColumnLayout`
- title label
- wrapped text block
- optional status row

Behavior:
- text changes update measured preferred height
- parent layout is invalidated
- the composite widget can ask for more height without hardcoded constants


## Recommended implementation rules

### Rule 1

No container may inspect child internals directly.

A container uses only the common layout contract:
- visibility
- measure result
- arrangement callback
- common sizing flags

### Rule 2

No visual widget may perform ad-hoc sibling placement when a layout container can express the same behavior.

### Rule 3

No layout code may depend on hardcoded component classes.

Layout must operate on generic nodes only.

### Rule 4

Measure must be pure with respect to geometry.

It may cache results, but it must not move windows or mutate unrelated state.

### Rule 5

Arrange must not re-enter measure unless explicitly requested through a constrained second pass.

This avoids unstable or recursive layout loops.


## Migration path

### Phase 1

Introduce the generic layout node contract and non-visual containers.

### Phase 2

Add one window helper host that can own one layout root and run deferred relayout.

### Phase 3

Wrap simple existing components with layout-aware measure callbacks:
- button
- label-like components
- clock widget

### Phase 4

Replace manual geometry code in composite widgets with row, column, grid, spacer, and size-box containers.


## Expected benefits

- one consistent geometry model across UI components
- robust handling of fixed and auto dimensions per axis
- clean parent propagation for content-driven size changes
- less manual `MoveWindow()` code in composite widgets
- reusable non-visual containers
- easier construction of dialogs, toolbars, forms, and panels
- better long-term compatibility with richer widgets
