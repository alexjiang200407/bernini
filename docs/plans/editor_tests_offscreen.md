# editor_tests_offscreen — implementation plan

## Context
`editor_tests` opens real windows on macOS and steals focus from whatever is being worked on: the
loading screens `util/Modal.cpp` drives and `AssetImporterDialog_test.cpp`'s `dialog.show()` land
on the desktop, in front of the editor the suite was run to check. `src/main.cpp` already defaults
`QT_QPA_PLATFORM` to `offscreen` for exactly this reason, but the default is compiled out: the
`EDITOR_TESTS_HAVE_OFFSCREEN` define that guards it is set inside the `if(WIN32)` block that
deploys the plugin, so no other platform ever gets it. It is the only suite affected — nothing in
`bgl_tests`, `gamelib_tests`, `assetlib_tests` or `core_tests` names a swapchain, a surface, an
`NSView` or a `QApplication`.

## Decisions
- **ADR-1 — The offscreen default stays in the binary, not in the test runner.** `main.cpp` is
  reached by `just test`, `just run editor_tests` and an IDE launch alike, and `-platform cocoa`
  still outranks it when the windows are what you want to watch. *Rejected: exporting
  `QT_QPA_PLATFORM` from `scripts/run_tests.py`, because it covers only the one entry point and
  puts a Qt rule in a runner that has no other Qt in it.*
- **ADR-2 — The define is gated on the plugin target, not on the platform.** Whether
  `-platform offscreen` is safe to name is a question about whether Qt can load the plugin —
  naming one it cannot aborts with a modal error box — so `TARGET Qt6::QOffscreenIntegrationPlugin`
  is the condition. The Windows-only part is the *deployment*: `windeployqt` ships only the plugin
  the binary would use interactively, while elsewhere Qt loads it from its own install prefix.
  *Rejected: widening the `if(WIN32)` to an if/else per platform, because that restates the same
  rule once per platform and is wrong again on the next one.*
- **ADR-3 — The platform is asserted by a test case rather than left to the eye.** A case pinning
  `QGuiApplication::platformName()` fails loudly if the define is lost again, which is how it was
  lost the first time. *Rejected: a `scripts_tests` case reading the CMakeLists, because it pins
  the build configuration rather than the behaviour and breaks on a legitimate refactor.*
- **ADR-4 — This follows Qt's own practice rather than deviating.** `QT_QPA_PLATFORM=offscreen` is
  what qtbase's suite and Qt CI run under; the mechanism here was already right and only its gate
  was wrong. No deviation to record.

## Non-goals
- Rewriting tests to stop constructing widgets. The widgets are what they pin.
- Any change to the other suites. None of them creates a window.
- A command-line switch of our own for the platform. Qt already consumes `-platform` out of `argv`
  before Catch2 sees it, and it already outranks the environment.

## Acceptance
- `just test editor` green on `macos-clang-metal-debug` with no window appearing on screen.
- `just run editor_tests -- "[platform]"` asserts the suite is on `offscreen` when nothing
  overrode it.

## Commits
1. `docs(plans): plan the editor_tests offscreen default` — this file.
2. `fix(editor): default editor_tests to the offscreen platform on every host` — move the define
   out of the `if(WIN32)` block onto the plugin target, and add the case that pins it.
   Gate: `just test editor`, and no window on screen while it runs.
