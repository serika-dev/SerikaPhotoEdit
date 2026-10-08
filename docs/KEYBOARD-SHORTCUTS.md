# Keyboard shortcuts

Serika uses one shortcut registry for menus, tools, tool cycling, canvas operations, blend modes and painting controls. Open **Edit → Keyboard Shortcuts** to search and change any registered binding. The editor records key combinations with a native key capture control, checks duplicate and prefix conflicts, and supports reset, import and export. Clear a binding to disable it. Changes apply immediately after Save.

Ctrl is the primary modifier on Windows and Linux. Qt displays and dispatches the same portable binding as Command on macOS; Alt appears as Option. The macOS build is configured but has not been tested on this Windows host.

Common defaults:

| Operation | Windows / Linux |
|---|---|
| New / Open / Save / Save As | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Undo / Redo / Step Back | Ctrl+Z / Ctrl+Shift+Z / Ctrl+Alt+Z |
| Free Transform / Commit / Cancel | Ctrl+T / Enter or Ctrl+Enter / Escape |
| New layer dialog / New layer directly | Ctrl+Shift+N / Ctrl+Alt+Shift+N |
| Layer via Copy / Layer via Cut | Ctrl+J / Ctrl+Shift+J |
| Group / Ungroup | Ctrl+G / Ctrl+Shift+G |
| Merge Down / Merge Visible / Stamp Visible | Ctrl+E / Ctrl+Shift+E / Ctrl+Alt+Shift+E |
| Select All / Deselect / Reselect / Inverse | Ctrl+A / Ctrl+D / Ctrl+Shift+D / Ctrl+Shift+I |
| Fit / Actual pixels / Zoom | Ctrl+0 / Ctrl+1 / Ctrl++ or Ctrl+- |
| Levels / Curves / Hue-Saturation / Color Balance | Ctrl+L / Ctrl+M / Ctrl+U / Ctrl+B |
| Select and Mask | Ctrl+Alt+R |
| Command palette / Keyboard shortcut editor | Ctrl+Shift+P / Ctrl+Alt+Shift+K |
| Brush size / Hardness | [ and ] / Shift+[ and Shift+] |
| Quick Mask / Layer mask overlay | Q / Backslash |
| Swap / Reset foreground and background | X / D |
| Hide all panels / Hide right panels | Tab / Shift+Tab |
| Temporary Hand / Temporary Move with a paint tool | Hold Space / Hold Ctrl |
| Move active layer / Move 10 pixels | Arrow / Shift+Arrow with Move active |

Tool keys select the last used variant in a tool group. Shift+tool key cycles through that group's variants. Individual variants can also receive their own bindings.

Digits set brush opacity when a paint tool is active, and layer opacity otherwise. Press two digits within 650 ms for an exact percentage, such as `3` then `7` for 37%. One `0` sets 100%; `0` then `0` sets 0%. Shift+digits set brush flow or layer fill. Text fields, number fields and combo boxes retain their normal typing, clipboard, selection and navigation keys.

Shift+Alt+letter blend shortcuts set the brush mode when a paint tool is active and the layer mode otherwise. Shift+plus and Shift+minus cycle modes. Behind and Clear are brush-only shortcuts on Shift+Alt+Q and Shift+Alt+R. All 27 layer modes can be assigned in the editor, including the four modes without a public default letter binding.

Crop has separate contextual bindings: `O` cycles its composition overlay, and `X` swaps width and height. These apply while Crop is active; the ordinary editor meanings apply with other tools. Enter commits a pending crop or transform, and Escape cancels its preview.

Ctrl+Shift+F opens Fade. Flatten Image is available as an assignable command without a conflicting default.

## Portable sets

The application saves `shortcuts.json` in Qt's application configuration directory. The file stores overrides over the shipped defaults. Stable command IDs distinguish direct commands from dialog commands. An empty array explicitly disables a shortcut; an absent ID retains its default.

```json
{
  "schema": 1,
  "application": "Serika PhotoEdit",
  "bindings": {
    "tool.brush": ["F7", "Ctrl+K, Ctrl+B"],
    "tool.magnetic-lasso": ["F8"],
    "editor.swap-colors": [],
    "command.new-layer-dialog": ["Ctrl+Shift+N"]
  }
}
```

Each key sequence may contain up to four chords. The chord timeout is 1.5 seconds. The editor shows three bindings per row and preserves additional bindings from imported sets. Import validates IDs, sequence syntax and conflicts before changing any active binding. Reset restores defaults, including disabled keys.

Default shortcut references: [Adobe keyboard shortcut settings](https://helpx.adobe.com/photoshop/desktop/get-started/settings-and-preferences/view-keyboard-shortcuts.html), [Adobe public keyboard reference](https://helpx.adobe.com/content/dam/help/en/pdf/photoshop_reference.pdf). Serika's interface icons and branding are original.
