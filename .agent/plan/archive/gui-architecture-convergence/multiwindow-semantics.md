# Multi-window semantics

> Phase F design contract. This preserves the owner boundary without claiming
> that a full native multi-window/docking manager already exists.

## Owners

```text
GUIApp
  ├─ active window id / activation order
  ├─ native window manager
  ├─ whole-app modal gate
  └─ cross-window drag coordinator

GUIWindowHost[id]
  ├─ native window + presenter + per-window pointer context
  └─ WidgetTree
       ├─ focus path / pointer path / capture
       ├─ Popup / Tooltip / DragIme layers
       └─ per-window popup and modal state
```

`WidgetTree` never stores an app-global focused widget, modal stack or drag
session. A tree may continue to own a local drag ghost and local hit/path
information while `GUIApp` coordinates a cross-window transaction.

## Active window and focus

1. Native activation moves the window id to the front of `GUIApp` activation
   order and makes it active.
2. Every window keeps its own focus path. Inactive windows retain it only
   while their focused node remains attached/visible.
3. Keyboard automation must name a target window; a one-window app may omit
   it and resolves to the primary window.
4. Closing/detaching an active window clears its focus/capture path before
   `GUIApp` selects the next activation-order window.

## Popup and modal scopes

- **Popup / tooltip:** always per-window and attached to that tree's system
  layer.
- **Per-window modal:** a `UIPopupOverlay`/route policy in one tree blocks
  that window only.
- **Whole-app modal:** `GUIApp` records an owner window and gates pointer/key
  injection to all other window hosts. It is not represented by duplicating a
  popup into every tree.
- Closing a modal restores/invalidates focus through the affected window's
  focus path, not a naked app-global pointer.

## Cross-window drag

```text
begin:  source window + source tree + payload
hover:  active pointer window chooses target tree/path
drop:   target tree accepts -> GUIApp commits payload
cancel: clear target highlight + source ghost/transient state
```

- Source tree owns its ghost until the app coordinator transfers/cancels it.
- Target tree owns only temporary highlight; it never owns source payload.
- A rejected target or window close cancels the transaction.
- Commit happens exactly once after target acceptance; the target tree then
  receives `onDrop`.

## Docking boundary

Docking/workspace/tab models live above `WidgetTree` and consume
`GUIApp` window ownership plus the specialized layout primitives. They do not
become box children, popup layers, or a new global widget tree.
