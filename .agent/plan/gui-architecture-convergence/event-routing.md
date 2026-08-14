# WidgetTree event-routing contract

> Scope: Phase C's single-window `WidgetTree` route model. This is runtime
> behavior, not a document-authoring format and not a future multi-window
> drag coordinator.

## Core model

One point resolves to exactly one widget (a **single topmost hit**); event
routing and hover ownership both derive from that widget's ancestor path. This
mirrors UE Slate / WPF / Qt / DOM. There is no multi-candidate fall-through
collection and no separate deepest-first hover scan — those were the source of
the historical hover bugs (a full-region split pane stealing a button's hover,
a popup shield stealing a menu bar item's hover).

```text
hitTestAt(point)   -> single topmost widget (or null)
buildPath(target)  -> TreeRoot -> ... -> target (ancestor chain)
route(path)        -> Preview (root->parent) -> Target -> Bubble (parent->root)
hoverOwner(target) -> deepest isHoverable() widget on the ancestor chain
```

## Hit test

`WidgetTree::hitTestAt(element, point, bForHover)` recurses zOrder-high-first,
children before self, and returns the **first** hit — it never collects a
second candidate.

- `!isHitTestableSubtree()` (Hidden / Collapsed / SelfHitTestInvisible) prunes
  the whole subtree.
- `cullsChildHits(point)` (scroll viewports) stops descent and tests only self.
- `hitTestSelf(point)` is the self-hit contract; default =
  `isHitTestableSelf() && hitTestLayoutRect(point)`. Layout hosts narrow it —
  `UISplitPane` reports a hit only over its divider strip, so a full-area rect
  never steals hover/cursor from an overlapping child (e.g. a toolbar button
  over the top padding).
- **Hover transparency**: `isHoverTransparent()` (default false) makes a widget
  swallow presses but lets the hover walk continue to a visible sibling beneath
  it. A non-modal `UIPopupOverlay` shield is the canonical case: it is
  invisible to the user, must dismiss on an outside press, but must NOT steal
  hover from the menu bar item underneath (menu-bar hover-switch relies on
  this). `bForHover=true` skips these shields; presses use `bForHover=false`.

Fall-through is a **hit-test** concern (`HitTestInvisible` /
`SelfHitTestInvisible` / `isHoverTransparent`), never a route concern.

## Delivery model

For an ordinary pointer event, `WidgetTree` resolves the single topmost hit and
routes along its ancestor chain:

```text
Preview: TreeRoot -> ... -> parent
Target:  target
Bubble:  parent -> ... -> TreeRoot
```

- `MouseMoved` routes with `bForHover=true` (through hover-transparent shields);
  presses route with `bForHover=false` (the shield swallows them).
- A handled `Stop` step ends the event (`HandledExclusive`).
- A handled `Pass` step reports `HandledPass` (the game layer still sees the
  event); it does **not** fall through to lower UI candidates — that behavior
  is gone. A widget that wants to fall through must declare it at hit-test time
  (`HitTestInvisible` / `SelfHitTestInvisible`).
- `previewInputEvent()` defaults passive; `handleInputEvent()` is target
  delivery; `bubbleInputEvent()` defaults to `handleInputEvent()`.

The resolved route holds shared references to its path during delivery, then
checks tree membership before each step, so a callback may safely detach its
own subtree; later phases skip no-longer-live nodes.

## Hover owner

After routing, `hoverOwnerAlongPath(target)` walks up from the (hover-aware)
hit to the first `isHoverable()` widget. It is deterministic and needs no
separate scan: a text child resolves to its hoverable button, a split divider
resolves to the split pane. Hover is updated only **after** routing because
enter/leave callbacks may mutate the tree (menu-bar hover-switch closes and
reopens overlays; opening a new overlay destroys the retired one).

## Route policies

| Policy | Target source | Delivery rule |
| --- | --- | --- |
| `HitTest` | single topmost hit (hover-aware for moves) | preview -> target -> bubble; Stop ends, Pass reports `HandledPass` |
| `PointerCapture` | current captor | skips hit discovery; `bViaCapture=true`; route still has parent path |
| `Focus` | current focused widget | preview -> target -> bubble; a handled focused target is exclusive regardless of pointer hit filter |
| `TabTraversal` | tree focus traversal | tree chooses next focusable in stable paint order; no widget route is dispatched |
| `DragSession` | active drag/drop session | tree owns move/release/press; drop-target selection is separate from ordinary pointer routing |
| `Popup` | a route whose path contains a non-modal `UIPopupOverlay` | popup layer is above content; shield swallows outside presses, lets hover through |
| `Modal` | a route whose overlay has `_bModal=true` | same popup-layer delivery with modal dimming/blocking (blocks hover too) |

## Transient-state safety (UE FWeakWidgetPath semantics)

- `_pointerPath` / `_focusPath` are `std::vector<std::weak_ptr<UIElement>>`;
  `getPointerPath()` / `getFocusPath()` lock and drop dead entries, returning a
  live `UIElement*` snapshot (never a dangling pointer).
- `pruneTransientState()` runs at the top of every `dispatchEvent` and drops
  focus / capture / hover / path entries that no longer point at a live,
  attached widget. The internal `TreeRoot` is kept — it is a legal path head
  but is never itself "attached" (it has no `_tree` back-pointer).
- `_focused` / `_captured` / `_hovered` stay raw pointers for the public API,
  but are cleared on detach (`clearTransientState`) and re-validated on each
  dispatch. The tree holds strong references to every attached widget, so an
  attached widget cannot be destroyed out from under a transient reference.

## Control semantics

| Control | Press / move / release behavior |
| --- | --- |
| `UIButton`, `UISlider`, `UISplitPane`, `UISelectableRow` | target starts pointer capture; release is routed to the captor even outside its rect |
| `UISplitPane` | self-hit only over the divider strip (resize cursor + drag capture); pane regions belong to children |
| `UIScrollViewport` | child is the target; wheel bubbles outward until the first viewport that can scroll consumes |
| `UIMenuBarItem` | target opens menu; hover route switches the open menu; popup shield is a `Popup` policy route |
| `UIPopupOverlay` / `UIMenu` | overlay owns focus while open; Esc and shield press dismiss; non-modal shield is hover-transparent, modal blocks hover |

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
