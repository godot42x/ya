# WidgetTree event-routing contract

> Scope: Phase C's single-window `WidgetTree` route model. This is runtime
> behavior, not a document-authoring format and not a future multi-window
> drag coordinator.

## Route state

- `WidgetPointerState` owns the last logical pointer position and whether it
  is known.
- `pointerPath` and `focusPath` are root-to-target live paths. They are
  cleared when a detached subtree contained a referenced node.
- `WidgetRouteTrace` stores names rather than pointers, so a target that
  closes/detaches during delivery still leaves a safe debug record.

## Delivery model

For an ordinary pointer event, `WidgetTree` collects deepest hit candidates in
topmost-first order. Each candidate gets:

```text
Preview: TreeRoot -> ... -> parent
Target:  target
Bubble:  parent -> ... -> TreeRoot
```

- A handled `Stop` step ends the current event immediately.
- A handled `Pass` step records `HandledPass` and permits the next lower
  candidate to run. This preserves transparent overlay behavior.
- `previewInputEvent()` defaults to passive.
- `handleInputEvent()` is target delivery.
- `bubbleInputEvent()` defaults to `handleInputEvent()` for compatibility:
  existing parents such as nested `UIScrollViewport` continue to consume
  after their child target declines.

The resolved route holds shared references to its original path during
delivery, then checks tree membership before each step. A callback may safely
detach/close its own subtree; later phases skip no-longer-live nodes.

## Route policies

| Policy | Target source | Delivery rule |
| --- | --- | --- |
| `HitTest` | deepest topmost hit candidate | preview -> target -> bubble; Pass may continue to lower candidates |
| `PointerCapture` | current captor | skips hit discovery; `bViaCapture=true`; route still has parent path |
| `Focus` | current focused widget | preview -> target -> bubble; a handled focused target is exclusive regardless of pointer hit filter |
| `TabTraversal` | tree focus traversal | tree chooses next focusable in stable paint order; no widget route is dispatched |
| `DragSession` | active drag/drop session | tree owns move/release/press; drop-target selection is separate from ordinary pointer routing |
| `Popup` | a route whose path contains `UIPopupOverlay` | Popup layer is above content; overlay shield consumes outside press/dismiss |
| `Modal` | a route whose overlay has `_bModal=true` | same popup-layer delivery with modal dimming/blocking semantics |

## Control semantics

| Control | Press / move / release behavior |
| --- | --- |
| `UIButton`, `UISlider`, `UISplitPane`, `UISelectableRow` | target starts pointer capture; release is routed to the captor even outside its rect |
| `UIScrollViewport` | child is the target; wheel bubbles outward until the first viewport that can scroll consumes |
| `UIMenuBarItem` | target opens menu; hover route switches the open menu; popup shield is a `Popup` policy route |
| `UIPopupOverlay` / `UIMenu` | overlay owns focus while open; Esc and shield press dismiss; close during target is route-safe |
| drag/drop | active drag intercepts pointer semantics; accepting drop target is selected from the hit target's ancestor chain |

## Diagnostics

`dumpWidgetTree()` includes:

```text
pointer { known, x, y, path[] }
focusPath[]
lastRoute {
  policy / policyName,
  target,
  path[],
  steps[{ widget, phase / phaseName, handled, hitFilter }],
  result / resultName
}
```

The route trace is the automation assertion surface. A visual route overlay is
optional diagnostic presentation; it must read this trace rather than
reimplement hit-testing or route discovery.

## Scenario assertions

GUI JSONL scenarios can place an assertion after a frame boundary:

```json
{"assert":{"widget":"CheckA","control":{"type":"checkBox","checked":false}}}
{"assert":{"lastRoute":{"policyName":"pointerCapture","target":"Counter"}}}
```

- `widget` selects a named node from `dumpWidgetTree()`. The remaining object
  is a partial expected subtree; omitted fields are ignored.
- Without `widget`, the expectation is matched against the whole tree dump.
- Numeric values may use `$gt`, `$gte`, `$lt` and `$lte`; use these for
  resize/drag geometry rather than hard-coding unstable float results:

  ```json
  {"assert":{"widget":"DemoScroll","control":{"offset":{"$gt":0}}}}
  ```
- Host assertion failure stops the scenario with a non-zero exit. Use it with
  checkpoints and a BMP diff: structure explains behavior, golden pixels
  explain presentation.
