# Editor plugin contracts

`editor_api` defines the public C++ contracts for native editor extensions. This is an interface
review milestone: the compiled sample and recording host exercise the contracts, but the editor
does not yet load plugins or dispatch through them. `AssetStore` does not yet consume registered
kinds. The headers at the linked paths are the source of truth; when this map disagrees, trust the
header and fix the map.

## Design choices

- **Separate document semantics from Qt presentation.** `assetlib_plugin_api` has no Qt, renderer
  or implementation-library dependency. A runtime module registers authored kinds; its editor
  module registers panels and presentation. Built-in `AssetType` and codecs are unchanged.
  This contract belongs in assetlib so the offline CLI and game can use a custom kind without
  loading Qt or its editor module. `IAssetKind` represents one file type, not an individual record.
- **General tabs require no document kind.** A project tool, including a future AI management tab,
  uses `PanelDesc`. `AssetEditorDesc` additionally supplies extensions and an asset-opening panel.
- **One project, one host.** A panel borrows that project's `IEditorHost`. The host exposes the
  existing `AssetStore` and lends graphics, scene and `game::AssetManager` only inside synchronous
  render-thread calls. Do not create a second store or renderer to service a panel.
- **The host owns presentation.** Plugins populate host-created viewports and describe thumbnails.
  A scene thumbnail names geometry (a mesh key or the built-in sphere), optional material override
  and optional camera; no camera means automatic framing. Plugins never advance the frame loop.
- **Identity is independent of language.** Panel, action and menu IDs are stable; labels retain
  translation context/key and fallback text. The host resolves them through a borrowed
  `ILanguageResolver` without registering contributions again. Each host owns its locale and copies
  module catalogs; there is no singleton. The resolver and CSV reader are implemented, but the
  production editor does not yet use them.
- **Matched-build C++ boundary.** STL and Qt types intentionally cross it. These are not interfaces
  for an arbitrary compiler or engine version. Entry-point aliases name factory signatures, not
  implemented loader functions; the future loader must verify compatibility before calling them.
  `editor_api` currently carries gamelib's public dependencies so clients can use the borrowed
  manager and store. Those libraries still use their existing linkage: do not build a production
  plugin DLL against this milestone until the shared SDK establishes their single ownership.

## Interface index

| Contract | Header | Role |
|---|---|---|
| `IAssetPlugin`, `IAssetKindRegistry`, `IAssetKind` | [IAssetPlugin.h](libs/assetlib/include/assetlib/IAssetPlugin.h) | Qt-free authored-kind registration and document operations |
| `IEditorPlugin` | [IEditorPlugin.h](libs/editor_api/include/editor_api/IEditorPlugin.h) | Register editor contributions at startup |
| `IEditorRegistry` | [IEditorRegistry.h](libs/editor_api/include/editor_api/IEditorRegistry.h) | Own deferred panel, editor, action, importer and thumbnail descriptors |
| `LocalizedText` | [LocalizedText.h](libs/editor_api/include/editor_api/LocalizedText.h) | Deferred label lookup with fallback |
| `ILanguageResolver`, `LanguageResolver` | [ILanguageResolver.h](libs/editor_api/include/editor_api/ILanguageResolver.h), [LanguageResolver.h](libs/editor_api/include/editor_api/LanguageResolver.h) | Borrowed lookup service and host-owned implementation |
| `TranslationCatalog`, `ReadTranslationCsv` | [TranslationCatalog.h](libs/editor_api/include/editor_api/TranslationCatalog.h), [translation_csv.h](libs/editor_api/include/editor_api/translation_csv.h) | Module data and optional CSV ingestion |
| `MenuDesc` | [IEditorRegistry.h](libs/editor_api/include/editor_api/IEditorRegistry.h) | Stable menu identity and parent, separate from its label |
| `EditorPanel`, `AssetEditorPanel` | [EditorPanel.h](libs/editor_api/include/editor_api/EditorPanel.h) | Project-scoped widgets, close veto and held assets |
| `IEditorHost` | [IEditorHost.h](libs/editor_api/include/editor_api/IEditorHost.h) | Project store, render dispatch and editor navigation |
| `IEditorViewport`, `RenderContext` | [IEditorViewport.h](libs/editor_api/include/editor_api/IEditorViewport.h) | Host presentation with access to its scene view on the render thread |
| `Thumbnail`, `ThumbnailScene` | [Thumbnail.h](libs/editor_api/include/editor_api/Thumbnail.h) | No preview, CPU image, or a scene the host renders |

Owning pointer aliases live beside their interfaces: `AssetKindPtr`, `AssetPluginPtr` and
`EditorPluginPtr`. Descriptor callback aliases live in `IEditorRegistry.h`; `RenderWork` and
`ViewportRenderWork` live beside `RenderContext`.

Recheck this table whenever the public files move.

## Topology

```mermaid
flowchart TD
    Runtime[Runtime plugin] -->|RegisterKinds| Kinds[IAssetKindRegistry]
    Editor[Editor plugin] -->|Register| Registry[IEditorRegistry]
    Registry -->|deferred factory| Panel[Project panel]
    Panel -->|borrows| Host[IEditorHost]
    Host -->|GetLanguageResolver| Language[Host-owned language resolver]
    Editor -->|AddTranslations| Registry
    Registry -->|copies catalogs| Language
    Host -->|GetStore| Store[AssetStore]
    Host -->|InvokeRender| Render[RenderContext]
    Host -->|CreateViewport| Viewport[IEditorViewport]
    Kinds -->|owns| Kind[IAssetKind]
```

The diagram is the contract ownership/call topology. The recording host supplies registration and
uses the concrete language resolver; there is no production registry or loader in this milestone.

## Threading and lifetime

Registration, factories, panels, actions, importers and host navigation run on the GUI thread.
An importer may arrange background CPU work, but must finish or join it before returning; these
callbacks do not grant a background task permission to outlive the project. Thumbnail descriptions
and document callbacks may run concurrently and must not access widgets or mutate shared state.

`InvokeRender` and viewport `Invoke` run synchronously on the render thread and propagate
exceptions to the caller. A closure must never wait on the GUI thread or retain the borrowed
context. Owned scene handles may be retained by a panel, but all access and release still belong
on the render thread, before its host dies. The host drains a viewport's pending draws before
destroying its view. Inactive tabs must suspend their viewports through `SetActive`.

The module stays loaded until process exit. Its plugin object outlives its descriptors and kinds;
those outlive all callbacks and panels using them. On project close, ask all panels `CanClose`
first; any refusal keeps the project alive. Then destroy panels and their viewport children and
join plugin work while the old host still exists. Destroy the host last. On another project, create
new panels against a new host; do not silently retarget stored references to old services.

## Risky contracts

- **Registration:** IDs are nonempty, plugin-qualified strings. Extensions are lowercase with a
  leading dot. Missing required callbacks, null kinds, duplicate IDs or conflicting extension claims
  are errors, including conflicts with built-ins. Registry implementations must discard all of a
  failed module's contributions. Panel/editor IDs share one namespace; other categories have their
  own ID namespaces. These requirements are not implemented by the recording test host.
- **Factories:** @pre a non-null parent and live project host. @post return a non-null widget
  parented to that parent, transferring Qt ownership to the host. Each registered panel/editor is
  one reusable tab per project. Factories are lazy; registration cannot access a project.
- **Labels:** keys match `[a-z][a-z0-9_]*`; contexts are dot-separated components of that form,
  qualified by the plugin. Fallback is nonempty display text. `LocalizedText::Resolve` calls the
  explicitly supplied resolver every time; it neither caches a string nor locates global state.
  IDs, extensions and stored asset keys are never translated. Lookup uses an exact context/key/locale
  match; unknown context, key or locale returns the descriptor fallback. The initial locale is `en`.
  Locale tokens begin with an ASCII letter and contain only ASCII letters, digits, `_` or `-`;
  matching is case-sensitive, without normalization or regional fallback.
- **Resolver ownership:** link the static `editor_localization` target in the host. It depends only
  on core and Qt Core; `editor_api` does not link its implementation into every client. Plugins borrow the
  const `ILanguageResolver` returned by their host and cannot register catalogs or select its locale
  through that interface. All calls, including catalog registration and locale changes, run on the
  GUI thread. The resolver outlives its borrowers; independent hosts may choose different locales.
  This Qt editor service does not implement the game runtime localization system.
- **Catalogs:** `AddTranslations` transfers a module catalog by value, with one catalog per context.
  The host copies strings and owns them beyond registration. `LanguageResolver::RegisterCatalog`
  rejects invalid contexts, keys, locales, empty translations, duplicate key/locale pairs and existing
  contexts, leaving the previous state intact. Omit missing translations instead of storing empty
  strings. A production registry must roll back catalogs with every other contribution if a module
  fails; the recording registry does not implement that transaction. Collect and validate a module
  before exposing its contributions.
  Catalog and entry lookup use `core::str::unordered_str_map`; entries are addressed by `locale/key`.
  The separator cannot occur in either component. Contexts remain separate registration units,
  allowing an entire catalog to be validated before it becomes visible.
- **Language changes:** changing the resolver locale affects the next lookup, not already displayed
  strings. The future host must re-resolve its menu, action and tab labels and notify plugin widgets
  on the GUI thread. The sample resolves its widget title when constructed; live widget refresh,
  catalog discovery, pluralization and parameter formatting are not implemented.
- **Menus:** the host supplies `c_FileMenuId` and `c_ToolsMenuId` before plugin registration.
  `AddMenu` creates a plugin-qualified ID with a localized label; empty parent means a root menu,
  otherwise the parent must already exist. Register parents before children. Reject duplicate IDs,
  reserved `editor.` IDs and missing parents; never find, merge or create menus by their labels.
  Menu IDs have their own namespace. Failed registration rolls back menus with other contributions.
- **Actions:** both callbacks are required. Empty extensions means a menu action with an empty
  selection; otherwise the action is a content-menu contribution, offered only when every selected
  key matches. A menu action requires an existing `menuId`; a content-menu action requires an
  empty `menuId`. The host owns the actual actions and menus.
  A plugin can target `c_FileMenuId` or another registered menu, but its actions are disabled without an open
  project because they borrow `IEditorHost`. Shell commands such as `File → Open Project` stay
  host-owned and available before a project opens; this API does not expose project switching to
  plugins. `AddAction` is a menu-bar/content-menu contract, not a toolbar-button contract.
- **Importers:** the source is an OS path, the destination a project folder key. Store operations
  own writes. The host reports thrown errors; a successful write calls `AssetChanged`.
- **References:** `ReadReferences` must validate the document and report every reference, even
  one whose target is absent. Each field token uniquely identifies an occurrence; the host treats
  tokens as opaque, normalizes targets through assetlib and expands directory moves. A rewrite
  receives replacement targets with those tokens against the same input bytes. It preserves
  unknown fields and never writes to disk. The host alone commits or rolls back the resulting bytes.
  Input spans borrow the original bytes without copying them. A returned vector owns the encoded
  result; keeping the original unchanged permits rollback if another document's rewrite fails.
- **Packing/migration:** registered kinds are authored documents; `includeInPack` defaults to true.
  `Migrate` validates and returns current-schema bytes even for a kind with no older schema.
  Custom derived cache formats are outside this initial contract. Existing typed codecs and store
  operations remain the only project I/O seam; their registration integration is still pending.

## Usage sketch

```cpp
auto plugin = sample::CreateEditorPlugin();
plugin->Register(registry);
// Later, while the plugin and registry still live:
auto* panel = registry.panels.front().create(host, &projectRoot);
panel->SetActive(true);
```

See the compiled [sample](examples/editor_plugin/sample.cpp) and its
[README](examples/editor_plugin/README.md). It links only the public API and a private JSON parser;
it has no editor implementation include path. It currently displays a selected document key, not
a working document editor. The fake host deliberately fails if it is asked for storage or graphics.

`just test editor_plugin` exercises deferred registration, Qt ownership, tab activation, held asset
replacement, deferred label lookup/fallback with unchanged menu routing, malformed-document refusal, reference rewriting and preservation of unknown fields.
Each public editor header is also compiled alone, with no PCH. The asset plugin header compiles
against its Qt-free target alone. Renderer scheduling, DLL ABI checks, registry validation and real
store/pack behavior require later integration tests; the fake host does not establish them.

The localization tests use the concrete resolver through the fake host. They cover host isolation,
owned catalog copies, CSV decoding, invalid-input refusal, fallback and unchanged routing. They do
not establish production registry transactions or live widget retranslation.

## CSV authoring

`ReadTranslationCsv` parses supplied UTF-8 bytes into the same catalog data plugins register;
it performs no file I/O. The caller supplies the module context separately, so every row belongs to
one namespace. CSV is an optional input format, not part of `ILanguageResolver` or registration.

```csv
key,en,zh_CN
open_file,Open file,打开文件
save_file,Save file,保存文件
```

The first column must be `key`, followed by one or more distinct locale columns. Rows must have the
same width and unique keys. Empty cells are missing translations. UTF-8 BOM, LF/CRLF records,
quoted commas/newlines and doubled quotes are supported; malformed UTF-8, NUL bytes, bad quoting,
duplicate keys/locales and invalid identifiers throw. Whitespace is preserved rather than trimmed.
Placeholders remain literal text; this importer supplies neither interpolation nor plural selection.
Resolve the returned catalog through `LanguageResolver` after registering it; plugins may instead
construct `TranslationCatalog` directly, as the sample does.
