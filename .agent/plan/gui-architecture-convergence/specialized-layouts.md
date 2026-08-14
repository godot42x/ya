# Specialized layout contract

> Phase E runtime baseline. Geometry belongs to `Runtime/Layout`; widgets
> retain content, paint, clipping and input semantics.

## Implemented layouts

| Layout | Host widget | Layout-owned data | Widget-owned data |
| --- | --- | --- | --- |
| `UISingleChildLayout` | `UIButton` | content padding, measure and assigned inset rect | button visual/input/focus state |
| `UISplitLayout` | `UISplitPane` | orientation, ratio, min extents, divider thickness, padding, content/divider rect and two-child arrangement | divider hover/drag transient state and divider colors |
| `UIScrollLayout` | `UIScrollViewport` | axis, offset, step, max offset, content desired extent and first-child arrangement | viewport clip paint and wheel route participation |

All three layouts are owner-attached `UILayout` objects. Their setters invalidate
the owner tree; no caller mutates a specialized widget's geometry fields.

## Single-child policy

`UISingleChildLayout` is the default content-control primitive:

- measure = first participating child desired size + padding;
- arrange = first participating child fills the padded content rect;
- extra children are intentionally not arranged by this lightweight layout.

`UIButton` uses it now. Popup/menu content remains a dedicated overlay policy,
not an excuse to create a duplicate single-child geometry implementation.

## Split policy

`UISplitLayout` arranges only the first two children. It clamps ratio to
min-first/min-second extents as soon as a valid content rect exists. During a
divider drag, `UISplitPane` updates layout ratio; `UISplitLayout` invalidates
arrange and the next snapshot lays out both panes. No GPU resource changes
occur in input delivery.

## Scroll policy

`UIScrollLayout` arranges only the first child:

- cross axis fills viewport;
- main axis takes `max(content desired, viewport extent)`;
- offset clamps to `[0, maxOffset]`;
- `scroll()` returns false at the limit so the explicit event route bubbles to
  an outer viewport.

## Diagnostics

`dumpWidgetTree()` emits `layout.type` for `box`, `singleChild`, `split` and
`scroll`, alongside control state for split ratio/divider and scroll
offset/max-offset. `ToolControlsTest.SpecializedLayoutsAppearInTreeDump`
guards this contract.

## Deliberately not specialized yet

`UIMenu`, popup overlay, tree rows and inspector rows remain composition-first:
their behavior is primarily popup/workspace/content policy, not reusable
geometry. Promote one only when a second independent consumer needs the same
measure/arrange contract.
