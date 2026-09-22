# Local editor plugin sample

Build this directory as its own CMake project against a matching engine build. It produces a
Qt-free runtime module for `.bexample` documents, an editor module with a project tool tab and a
selected-document tab, and a Qt-free archive reader. The document tab displays the selected key;
it is not a JSON authoring UI. There is no AI or quest implementation here.

## Build and load

First build the engine with the editor and SDK enabled (the top-level editor defaults):

```sh
just build editor
```

Configure separately, using the same compiler, architecture, configuration, dependency prefix and
vcpkg toolchain/triplet as the engine. Replace the paths below with that build's paths; on Windows,
run in the matching MSVC developer shell. For example with a Debug Ninja build:

```sh
cmake -S examples/editor_plugin -B /path/to/sample-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBerniniEditorSDK_DIR=/path/to/engine-build/editor_sdk \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/lib/cmake;/path/to/engine-build/vcpkg_installed/<triplet>"
cmake --build /path/to/sample-build --config Debug
```

Use `-DCMAKE_TOOLCHAIN_FILE=... -DVCPKG_TARGET_TRIPLET=...` too when your dependency setup needs them.
For a multi-configuration generator omit `CMAKE_BUILD_TYPE` and select the engine's configuration
with `--config`. The output directory is `sample-build/plugin/Debug` (or your chosen configuration).
It contains `bernini-plugin.json`, generated with the matching engine build ID and actual binary
filenames. Never copy an ID from a different SDK or manually touch binaries to bypass freshness.

The engine's own build already does all of the above for the test tree: `just build editor` builds
this sample against the current SDK and stages its output into `plugins/sample.document/` beside
the editor executable, where the editor loads it at startup with nothing configured. A sample built
elsewhere is reached by adding its **absolute** output directory to `pluginDirectories` in the
editor's runtime `config.json` (the directory printed by `just exes --target editor` contains that
file):

```json
"pluginDirectories": ["/path/to/sample-build/plugin/Debug"]
```

Copy `project/` somewhere writable, then launch that copy:

```sh
just run editor -- --project /path/to/copied-project/Sample.bproj
```

**Plugins → Loaded Plugins** lists it as "Sample Document" once it loaded.
The project names `sample.document` in its `plugins` list, which loads nothing: it is what makes an
editor without the plugin refuse the project instead of opening it with its documents unreadable.
Another project needs no entry to use the tools; add one only to protect its `.bexample` files.
Open **Tools → Sample tools → Project tools** to see the project tab. Open `Holder.bexample` in
Content Explorer to see its document tab. Close the document tab before trying rename/delete:
open documents are protected independently of their saved references. Deleting `Target.bexample`
is refused because Holder references it. Rename Target and inspect Holder on disk: its reference
changes and its `note` remains.

## Document and runtime

A `.bexample` is a JSON object with a `references` array of nonempty mount keys:

```json
{"references": ["Target.bexample"], "note": "unknown fields survive rename and migration"}
```

The runtime kind validates documents, reports each reference occurrence separately, and preserves
unknown fields when rewriting references or migrating. The host performs file transactions and
packing. Both sample documents are included in the archive.

`sample_readback <archive.bpak> <document-key>` registers the same runtime kind in a Qt-free
program, reads the document through an archive-backed store, prints its referenced keys and fails
if any target is missing. Run it with the engine runtime libraries on the loader search path;
on Windows, put the engine executable directory on `PATH`. It uses the same runtime implementation
compiled into the sample module, without linking the editor module.

## Rebuild and restart

Build the engine first after changing SDK headers/libraries, then run the sample build again.
With Ninja/Make, the SDK stamp is a link dependency, so an otherwise unchanged sample relinks when
its SDK rebuilds. Other generators require `cmake --build /path/to/sample-build --config Debug --clean-first`
after an SDK rebuild, including changes to headers the sample does not directly include.
Completely exit and relaunch the editor to use rebuilt code: modules remain loaded until process
exit. On Windows the loader runs copies, leaving the original output files writable. Network
installation and hot reload are separate features.

## Verification and ownership

```sh
just test editor_plugin editor assetlib
```

The test build configures this sample independently with `find_package(BerniniEditorSDK)`.
Production-loader tests use its generated descriptor, instantiate its owned contributions, check
reference protection and duplicate-occurrence rename, preserve unknown fields through migration,
pack, delete the loose data and read the archive in the Qt-free subprocess. Separate ABI fixtures
under `tests/editor_sdk_consumer` test process services and loader failure cases.

Contract tests also compile these sources as a static fixture against a fake host. Neither build
uses a PCH or includes `apps/editor/src`. The runtime links only the Qt-free contract and a private
JSON parser; the editor links the public plugin API. See [the contracts](../../docs/editor_plugins.md)
for ownership and threading.

Factories and actions are owned, nonmovable contribution objects. Configuration is stored by
value; the host is supplied at invocation. The sample contributes a module translation catalog
and resolves widget text through the host's borrowed language resolver. Titles resolve at
construction; live refresh is deferred. The document panel reports its selected key as held and
handles matching asset-change notifications without reopening it.
