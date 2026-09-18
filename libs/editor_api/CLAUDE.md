# editor_api

Public plugin contracts, held to the strict library bar. Qt belongs here because this target is
editor-only; asset-kind semantics belong in assetlib's Qt-free plugin contract.

Headers must compile individually without a PCH and without apps/editor/src on the include path.
editor_api_selfcheck enforces that. No implementation or production registry lives here yet.
editor_plugin_tests exercises examples/editor_plugin through a recording host, with no GPU work.

See docs/editor_plugins.md for ownership, threading, and the integration still pending.
