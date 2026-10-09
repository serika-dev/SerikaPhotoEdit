# Local MCP server

Serika PhotoEdit includes a native stdio Model Context Protocol server. It uses the
same document, compositor, actions and file-format code as the editor. Each client
connection owns isolated in-memory documents; it does not control existing GUI tabs.
No network listener, model account or additional runtime is needed.

Start the installed executable with an existing folder dedicated to your work:

```powershell
SerikaPhotoEdit.exe --mcp --mcp-root "C:/Pictures/SerikaWorkspace"
```

On macOS, use `SerikaPhotoEdit.app/Contents/MacOS/SerikaPhotoEdit`. Linux packages
provide the `SerikaPhotoEdit` launcher. Configure your MCP client to launch the
command directly with the argument array shown in
[the example configuration](../examples/serika-photoedit.mcp.json). Replace both
absolute paths for your installation. The client manages stdin/stdout; do not
launch a second GUI wrapper or shell command string around it.

The server implements initialization, ping, tools/list and tools/call, with
protocol negotiation for 2025-11-25, 2025-06-18 and 2025-03-26. Messages are
newline-delimited UTF-8 JSON-RPC. Stdout contains only protocol messages;
diagnostics go to stderr. Notifications do not produce responses or execute edits.

## Tools

| Tool | Behavior |
| --- | --- |
| `server_status`, `list_documents` | Workspace, limits, capabilities and session documents |
| `create_document`, `open_document`, `close_document` | RGB documents at 8, 16 or 32 bits; close with unsaved edits needs `discard: true` |
| `inspect_document` | Stable document/layer IDs, hierarchy, appearance, parameters and undo state |
| `preview_document` | Composite PNG image content, bounded to 32–2048 pixels on the longest edge |
| `save_document` | Editable SPE/SPEB, compatible PSD/PSB, or flattened raster export |
| `select_layer`, `set_layer_properties` | Active layer, name, opacity, visibility and blend mode |
| `add_layer`, `create_text_layer` | Editable pixel/group/fill/text content, including native typography parameters |
| `run_actions` | 1–100 native action steps in one undoable transaction; failure restores the before-state |
| `undo`, `redo` | Session edit history |

Use the returned IDs instead of guessing layer indices. Save SPE as the editable
master; exporting a raster or PSD does not mark the native document saved. Call
`preview_document` to inspect work before saving. No writes occur merely because
a document was created or edited. Existing files require `overwrite: true`.

Action steps follow [the action format](../examples/web-export.speaction):

```json
{"document_id":"<returned-id>","steps":[
  {"command":"New Layer","parameters":{"name":"Retouch"}},
  {"command":"adjustment","name":"Exposure","parameters":{"exposure":0.3}}
]}
```

`create_text_layer` uses pixels for its base `font_size`, `x`, `y` and `tracking`. Rich text uses `parameters.typography` documented in [Typography](TYPOGRAPHY.md); span `size` values are points.
Fill parameters use the same settings as the native gradient/pattern editors.
Unsupported action commands return an error; shell commands and arbitrary scripts
are not implemented.

## Scope and limits

File paths must resolve inside the configured workspace, including through
symbolic links. Output parent folders must already exist. The session allows 8
documents, 16 million pixels per document, 30,000 pixels per dimension, 256 layers,
512 MB of stored image/mask/embedded content per document, 20 undo states and 4 MiB
per request. Native decoders and transforms also enforce their own bounds. These
are application limits, not an operating-system memory sandbox; full-image
operations and undo copies can require more working memory.

MCP rejects SVG/SVGZ inputs, including files detected as SVG under a different
extension, because Qt's SVG decoder can read external image references. The GUI
still supports SVG. Linked Smart Objects opened through MCP use their stored
cached pixels; the server exposes no linked-source refresh operation. Resize
defaults are checked against each preceding action's resulting dimensions, and
transform raster bounds are checked before allocation. A rejected action set
restores the document and its undo history.

The desktop remains an RGB editor. MCP uses its normal rendering and format
behavior, including the documented PSD fallbacks. GPU acceleration is optional
via `--gpu` and depends on the platform plugin/device; headless platforms commonly
fall back to CPU. See [GPU processing](GPU-PROCESSING.md).

Protocol references: [stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
