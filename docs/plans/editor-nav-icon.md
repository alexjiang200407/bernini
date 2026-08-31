# editor-nav-icon — implementation plan

## Context

The Content Explorer's Back button asks for `QStyle::SP_ArrowBack`
(`apps/editor/src/Windows/ContentExplorer/ContentExplorerWindow.cpp:62`). The macOS style does not
answer that pixmap, so the request falls through to `QCommonStyle`, which returns Qt's own bundled
arrow bitmap — a saturated green raster that ignores the palette and does not scale. Beside a native
combo box in a dark editor it reads as a stray asset, in the panel artists spend the most time in.

Every icon in the editor comes from `style()->standardIcon`; there is no `.qrc` and no icon
resource of any kind.

## Decisions

- **ADR-1 — The editor decides its own navigation glyph and paints it from the palette, rather than
  accepting the toolkit's stock pixmap.** This is what every shipping editor does: Unreal's Slate
  `FSlateStyleSet` recolours SVG icons by style token, Unity ships `d_`-prefixed dark-skin variants,
  Godot recolours its editor icons per theme at runtime. *Rejected: an SVG icon set behind a `.qrc`,
  because it buys an asset pipeline and a per-glyph design job to place one arrow.* *Rejected:
  dropping the icon for the `Back` text the `.ui` already carries, because no DCC uses a text-only
  navigation button and it widens the button past the chrome beside it.*

- **ADR-2 — The glyph is a `standardIcon` override on the existing `EditorStyle` proxy, not a
  `setIcon` at the call site.** `EditorStyle` already exists as "the platform's style, with the
  rules that make a docked layout awkward overridden", and `standardIcon` is a `QStyle` virtual, so
  the rule lands in the one place that already owns this kind of exception. *Rejected: setting the
  icon in `ContentExplorerWindow`, because it makes the call site a second place an editor icon can
  come from, and the next panel copies whichever it finds first.*

- **ADR-3 — The override answers `SP_ArrowBack` and `SP_ArrowForward`; every other standard pixmap
  is still the platform's.** The two are one navigation pair, and `SP_ArrowBack` already resolves to
  a right-pointing arrow under RTL, so the engine must point both ways regardless — the pair costs a
  case label. *Rejected: answering `SP_ArrowBack` alone, because a style whose rule is "we theme
  exactly one pixmap" breaks silently the day a Forward button lands beside it.* *Rejected: the
  whole arrow family or every stock pixmap, because those are drawn by call sites this change has
  not audited.*

- **ADR-4 — The icon paints on demand through a `QIconEngine` rather than pre-rendering a pixmap.**
  The colour is then read from the palette at paint time and the glyph is rasterised at the size and
  device pixel ratio actually asked for. *Rejected: `QIcon::addPixmap` of a pre-rendered chevron,
  because the colour bakes at construction and a macOS light/dark switch leaves it stale — which is
  the exact failure being fixed.*

## Non-goals

- The Animation editor's transport bar (`AnimationEditorWindow.cpp:219/237/246` —
  `SP_MediaPlay`, `SP_MediaSeekBackward`, `SP_MediaSeekForward`) has the same stock-bitmap problem
  and is deliberately left alone.
- No editor icon set, no `.qrc`, no stylesheet or theme work.
- No Forward button is added; `SP_ArrowForward` is answered, nothing requests it yet.

## Acceptance

- `just run editor_tests -- "[editorstyle]"`:
  - every opaque pixel of `EditorStyle::standardIcon(SP_ArrowBack)` is the palette's `ButtonText`
    colour — which Qt's green bitmap fails;
  - re-rendering under a different `ButtonText` changes the pixels, proving the colour is read at
    paint time and not baked;
  - a pixmap asked for at device pixel ratio 2 or 3 comes back at that resolution, pinning the
    guarantee ADR-4 makes on the one platform this is about;
  - `SP_MediaPlay` still comes back identical to the platform style's, pinning the non-goal.
- Eyes: the arrow reads as monochrome chrome beside the "Assets" combo, and greys out when there is
  no history to go back to.

## Commits

1. `docs(plans): plan the editor's navigation arrow` — this file.
2. `fix(editor): paint the navigation arrow from the palette` — the `QIconEngine` and the
   `standardIcon` override in `EditorStyle`. No call site changes: `ContentExplorerWindow` already
   asks its own style for the pixmap, so the override is what it receives.
   Gate: `just run editor_tests -- "[editorstyle]"`.
