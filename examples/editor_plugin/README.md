# Editor plugin contract example

This compiled client exercises the proposed public interfaces before the production loader exists.
It is a static test fixture, not an installable plugin or a binary SDK example yet.

The runtime half registers `.bexample`, a JSON document whose `references` array contains mount
keys. It preserves unknown fields when migrating or rewriting individual reference occurrences.
The editor half contributes an ordinary project tab, an asset tab that displays the selected key,
and a Tools menu action. There is no AI or quest implementation here.

Build and run from the engine checkout:

```sh
just test editor_plugin
```

The example compiles without a PCH and cannot include `apps/editor/src`. Its only public dependency
is `editor_api`; nlohmann JSON is private. The suite supplies the recording registry and host, with
no storage or rendering implementation. See [the contracts](../../docs/editor_plugins.md) for the
ownership and threading rules. Local DLL loading and a separately configured SDK consumer follow
in later feature tasks.

Menu registration uses stable IDs and context/key/fallback labels. The sample contributes a
submenu beneath `editor.tools`; its action targets that submenu ID regardless of displayed language.
The sample registers its own catalog and resolves its widget title using the host-provided
`ILanguageResolver`. The fake host owns a concrete `LanguageResolver`; the tests also exercise the
optional CSV reader. Widget titles resolve at construction, with live refresh still deferred.
