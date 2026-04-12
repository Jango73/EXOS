# Mouse Cursor Draw Profiling

## Capture

Profiling capture taken with the fast deferred work queue configured at 2 ms.

| Counter | Calls | Average | Maximum | Total |
| --- | ---: | ---: | ---: | ---: |
| Mouse.QueueToDeferred | 34 | 2000 us | 2000 us | 68000 us |
| Mouse.DeferredWork | 34 | 777 us | 1662 us | 26444 us |
| Mouse.DispatcherOnInput | 34 | 747 us | 1455 us | 25425 us |
| Desktop.CursorPositionChanged | 29 | 748 us | 1038 us | 21713 us |
| Desktop.CursorHardwareSetPosition | 10 | 43524 us | 197266 us | 435242 us |
| Desktop.CursorSoftwareRedraw | 30 | 689 us | 996 us | 20671 us |
| Desktop.CursorSoftwareOverlay | 97 | 517 us | 995 us | 50176 us |
| Desktop.DrawRequestToDispatch | 130 | 2807 us | 8000 us | 607000 us |
| Desktop.DrawDispatch | 130 | 970 us | 39764 us | 126495 us |
| Desktop.ClientDrawCallback | 102 | 7496 us | 201893 us | 764966 us |
| Desktop.SystemChromeDraw | 10 | 43524 us | 197266 us | 435242 us |
| Desktop.RequestWindowDraw | 183 | 43 us | 191 us | 8028 us |
| ConsoleCursorPosition | 34 | 71 us | 343 us | 2419 us |
| ConsolePrintChar | 91 | 112 us | 452 us | 10275 us |

## Reading

The fast deferred work cadence is visible through `Mouse.QueueToDeferred`: every
recorded sample is 2000 us. This means the measured mouse input wait before the
deferred work task matches the configured 2 ms queue period.

The first mouse processing stages stay under 2 ms in this capture:

- `Mouse.DeferredWork`: 777 us average, 1662 us maximum.
- `Mouse.DispatcherOnInput`: 747 us average, 1455 us maximum.
- `Desktop.CursorPositionChanged`: 748 us average, 1038 us maximum.
- `Desktop.CursorSoftwareRedraw`: 689 us average, 996 us maximum.

The larger delays appear after cursor movement requests enter the desktop draw
path:

- `Desktop.DrawRequestToDispatch` averages 2807 us and reaches 8000 us.
- `Desktop.DrawDispatch` averages 970 us but reaches 39764 us.
- `Desktop.ClientDrawCallback` averages 7496 us and reaches 201893 us.
- `Desktop.CursorHardwareSetPosition` and `Desktop.SystemChromeDraw` share the
  same count and duration distribution in this capture, which requires checking
  whether the measurement scopes overlap the same work or whether the hardware
  cursor command is genuinely waiting behind the same operation.

## Investigation Plan

1. Split `Desktop.ClientDrawCallback` by window class or process.
   The largest maximum is in the client draw callback. The next profiling point
   should identify which window function accounts for the expensive samples.

2. Split `Desktop.DrawDispatch` into preparation, non-client draw, client draw,
   overlay draw, and completion.
   The dispatch maximum is much higher than its average, so the expensive path is
   likely conditional.

3. Split `Desktop.SystemChromeDraw` into title bar, frame, controls, theme lookup,
   and final blits.
   Its duration matches `Desktop.CursorHardwareSetPosition` in the capture, which
   makes the boundary between these scopes suspicious.

4. Check `Desktop.CursorHardwareSetPosition` scope placement.
   A hardware cursor position update should not require tens of milliseconds in
   normal operation. Confirm whether the scope only covers the backend position
   command and not surrounding redraw work.

5. Record per-window `Desktop.DrawRequestToDispatch`.
   The 8000 us maximum can come from queue pressure, message ordering, or a
   specific window repeatedly requesting redraws. Add window class or window
   identifier tagging before optimizing the scheduler path.

6. Compare software cursor and hardware cursor paths separately.
   Keep separate counters for hardware cursor movement, software invalidation,
   software overlay rendering, and any fallback path selection.

7. Re-run after clearing the profile table.
   Use a short capture after `prof reset`, move only the mouse, then read `prof`
   before unrelated console output or window activity pollutes the sample set.

## Capture After Low-Level Draw Profiling

Profiling capture taken after adding low-level line and rectangle counters.

| Counter | Calls | Average | Maximum | Total |
| --- | ---: | ---: | ---: | ---: |
| Mouse.DeferredWork | 31 | 2000 us | 2000 us | 62000 us |
| Mouse.DispatcherOnInput | 31 | 730 us | 1526 us | 22630 us |
| Desktop.CursorPositionChanged | 22 | 705 us | 1066 us | 19045 us |
| Desktop.CursorSoftwareDraw | 77 | 667 us | 1027 us | 18678 us |
| GFX.Line | 21 | 807 us | 8746 us | 16952 us |
| GFX.Line.Software | 21 | 806 us | 8746 us | 16924 us |
| GFX.Rectangle | 97 | 518 us | 10812 us | 244306 us |
| GFX.Rectangle.Software | 97 | 521 us | 10817 us | 243865 us |
| Desktop.DrawRequestToDispatch | 100 | 9180 us | 270000 us | 918000 us |
| Desktop.DrawRequestToDispatch.Window.Other | 81 | 92962 us | 690000 us | 7530000 us |
| Desktop.DrawRequestToDispatch.Class.Root | 30 | 63333 us | 610000 us | 1900000 us |
| Desktop.DrawRequestToDispatch.Class.ShellBar | 19 | 86842 us | 165000 us | 1650000 us |
| Desktop.DrawRequestToDispatch.Class.ShellBarSlot | 12 | 11000 us | 64000 us | 132000 us |
| Desktop.DrawRequestToDispatch.Class.LogViewer | 7 | 92857 us | 390000 us | 650000 us |
| Desktop.DrawDispatch | 100 | 1048 us | 30018 us | 104868 us |

## Reading After Low-Level Draw Profiling

The fast mouse deferred work queue is still visible at 2 ms:
`Mouse.DeferredWork` records 31 samples at 2000 us average and 2000 us maximum.

The cursor software path does not explain the largest perceived latency in this
capture. `Desktop.CursorSoftwareDraw` averages 667 us and reaches 1027 us. The
low-level software primitives used by the cursor and chrome are measurable but
also not the dominant delay by themselves: `GFX.Line.Software` averages 806 us,
and `GFX.Rectangle.Software` averages 521 us.

The large delays are in redraw queuing and dispatch timing. The global
`Desktop.DrawRequestToDispatch` counter averages 9180 us and reaches 270000 us.
The per-class counters show much larger delayed redraw requests:

- `Desktop.DrawRequestToDispatch.Window.Other`: 92962 us average, 690000 us
  maximum.
- `Desktop.DrawRequestToDispatch.Class.Root`: 63333 us average, 610000 us
  maximum.
- `Desktop.DrawRequestToDispatch.Class.ShellBar`: 86842 us average, 165000 us
  maximum.
- `Desktop.DrawRequestToDispatch.Class.LogViewer`: 92857 us average, 390000 us
  maximum.

This capture points away from the direct cursor draw as the main source of the
latency. Mouse input is scheduled quickly, and the software cursor draw stays
around one millisecond. The suspicious path is the desktop redraw queue: some
window redraw requests wait hundreds of milliseconds before dispatch, and those
redraws can visually compete with cursor movement even if the cursor draw itself
is short.

The next investigation step is to isolate why `Desktop.DrawRequestToDispatch`
builds such large per-window delays. Focus on queue pressure, redraw request
ordering, repeated invalidation of root or shell bar windows, and whether stale
queued redraws are still processed after newer cursor movement has already been
handled.
