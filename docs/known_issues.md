# Known Issues — bugs that were fixed, and how to tell if one is back

One entry per bug that cost somebody a day and could plausibly return: what it looked like from the
outside, what actually caused it, what closed it, and **what to check first if the same symptom
appears again**. The point is that the second person to see a symptom recognises it in a minute
instead of re-deriving it from a stack trace.

An entry here is about a symptom, and it earns its place by being hard to diagnose rather than by
being recent. Delete one when the code it warns about is gone — when a backend is retired, so is its
entry.

Each entry states the gates that pin the fix. **If a gate is green and the symptom is back, the
entry's cause is not this one** — that is the fastest thing the entry can tell you, and it is why the
gates are named rather than described.

---

## `core_tests` hangs on one shard, at full CPU, with no failing case named

**Symptom.** `just test` never finishes: one `core_tests` shard sits for hours with the suite lock
held, and beside it a second `core_tests` process at 100% CPU. Sampled, the shard is in
`CrashLog_test.cpp` → `wait4`; the child it forked is in the crash handler, at
`crash_log_path()` → `localtime_r` → `tzsetwall_basic` → `notify_register_tz` →
`notify_register_check` → two frames inside libsystem_notify → `_dispatch_once_wait`. Which
shard, and whether at all, depends on the random test order: the suite is sharded across one
random order, and the case lands wherever the partition puts it.

**Cause.** The handler built its stamp with `localtime_r`. On macOS the first `localtime` in a
process initialises the time zone through libnotify under a `dispatch_once`, and that state does
not survive a `fork`: a child whose parent never called `localtime` waits on it forever. The crash
tests fork a child to crash on purpose, so an order in which nothing before them had touched the
time zone hung the child in the handler and the parent in `wait4`. The same call is not
async-signal-safe in any process: a crash on one thread while another holds the time-zone lock
deadlocks the handler with no log written.

**Fixed by** the handler stamping from `time()` and integer arithmetic alone, in local time from an
offset `install_crash_handlers` reads once, on the installing thread
([util.cpp](../libs/core/src/err/util.cpp), `crash_log_path`).

**Gates.** `just run core_tests -- "[crashlog]"`: the stamp is checked against the wall clock the
parent reads the ordinary way, and the crash tests install the handlers in the parent, so the
forked child calls no time function at all.

**If it comes back.** Read the signal path from `crash_signal_action` to the file name first:
nothing on it may call `localtime`, `gmtime`, `strftime` or `tzset`, and a new stamp, a new
timestamp in the log body, or a logger reached from the handler is where one would arrive. The
allocations the handler already makes (`std::ofstream`, `std::format`, cpptrace) are a different
hazard: they can deadlock on a crash inside `malloc`, never wait forever on the time zone.

---

## `assetlib_tests` hangs on one shard, at *no* CPU, inside a texture bake

**Symptom.** `just test` never finishes: one `assetlib_tests` shard sits for hours holding the suite
lock while every other shard passes. The discriminator against the `core_tests` hang above is the
CPU — that one spins at 100%, this one consumes **nothing**. Sampled minutes apart its consumed time
does not move (17s after 83 minutes) and its footprint stays near 6 MB. Sampled, the shard is in a
bake, and the bottom of the stack is the whole story:

```
MaterialBake_test.cpp → AssetStore::BakeMaterial → bakeMaterial
  → writeKTX2 → buildKtx → compressBasis (image_io.cpp)
    → ktxTexture2_CompressBasisEx → basisu::job_pool::~job_pool()
      → std::thread::join() → __ulock_wait
```

**Cause.** A lost wakeup in basisu's thread pool, vendored inside libktx and none of ours.
`job_pool::~job_pool` (`external/basisu/encoder/basisu_enc.cpp`) sets its kill flag and notifies
**without holding the pool's mutex**, then joins:

```cpp
m_kill_flag = true;        // std::atomic<bool>, but not under m_mutex
m_has_work.notify_all();   // notified without the lock
for (...) m_threads[i].join();
```

A worker waits on that condition variable under that mutex —
`m_has_work.wait(lock, [this]{ return m_kill_flag || m_queue.size(); })`. A worker that evaluates the
predicate false and is descheduled before it enqueues on the condition variable never receives the
notification, then sleeps on a flag that is already true; the destructor's `join()` never returns.
`std::atomic` removes the data race on the flag, not the lost wakeup: closing that window needs the
destructor to hold `m_mutex` while setting the flag, and it never takes it.

The window is a few instructions wide, so it takes load to land in — `just test` shards
`assetlib_tests` four ways and each shard asks for `hardware_concurrency()` basisu threads
(`bp.threadCount`, [image_io.cpp](../libs/assetlib/src/image_io.cpp)), so a 12-core machine runs 48
workers and a thread descheduled inside that window stays there for a scheduler quantum rather than
nanoseconds.

**How rare it is, measured.** Sharding alone is *not* enough to bring it out: three consecutive
`just test assetlib_tests` runs on an otherwise idle machine all passed, and the two unsharded
`just run assetlib_tests` runs seen either side of the failure passed too. The one occurrence was a
`just test` while a second checkout was also working the machine. So do not expect to reproduce it
on demand, and do not read a clean run as evidence it is gone — the suite lock serialises *suites*
across checkouts, not builds, so a sibling checkout compiling beside your suite is load the lock
does not exclude.

**Fixed by** taking `m_mutex` around the kill flag before notifying, in
[cmake/ports/ktx/0007-basisu-job-pool-deadlock.patch](../cmake/ports/ktx/0007-basisu-job-pool-deadlock.patch).
It had to be a patch: nothing bernini owns ever holds the pool, which is built and destroyed inside
`ktxTexture2_CompressBasisEx`, so the only reachable seam is the dependency. vcpkg's `ktx` port
already applies six patches, so this is a seventh, carried by an overlay port under
[cmake/ports/ktx](../cmake/ports/ktx) that is otherwise vcpkg's port verbatim. It is a backport —
upstream basis_universal already takes the lock, and adds a one-second `wait_for` timeout on top.

Dropping `bp.threadCount` to 1 also removes it, since `job_pool` spawns no workers below two threads,
and was rejected: single-threaded UASTC encoding of a 4K mip chain is too slow to ship.

**Gates.** The build itself, and deliberately not a test. A race that survived three consecutive
sharded runs cannot be pinned by a case that passes either way — such a test buys false confidence
and would be worse than none. What pins the fix instead is that **vcpkg fails the build when a patch
does not apply**: bump `ktx` past a basisu that already carries the lock and the port stops
configuring, by name, which is the signal to delete the overlay rather than a silent revert.

Do not read `just run assetlib_tests -- "[ktx2][threading]"` as this gate. That case covers
`be6ad7ed`, which serialised Basis codec **init** behind `basisInitMutex` — a different phase whose
green says nothing here. The sampled frame was `image_io.cpp`'s `g_Ready` fast path, which takes no
lock at all, so init had long since succeeded and this hang was in pool teardown after an encode that
worked.

**If it comes back.** Check the overlay is still in the build before anything else: the port under
`cmake/ports/ktx` and its `overlay-ports` entry in `vcpkg-configuration.json` are both load-bearing,
and deleting either silently restores the upstream destructor. Then sample the wedged process before
killing it — `job_pool::~job_pool` under `ktxTexture2_CompressBasisEx` at flat CPU is this one, and a
stack anywhere else is not. Kill it promptly whatever the cause: it holds the machine-wide suite lock
and every other checkout queues behind it.

---

## The editor's viewport is more saturated than the same frame anywhere else

**Symptom.** On a Mac, the material editor's viewport reads more saturated than Blender's Material
Preview of the same model under the same environment, reds first: fur binned by luma has more red
at the same brightness and the same green and blue. Every headless measurement agrees with Blender
— `ScreenshotPng` captures, the `[parity]` sphere, a flat grey at any albedo — and only the screen
does not. Seven measured changes to lighting, tone map, textures and the resolve did not move it.

**Cause.** The window's colour space was never set. Qt's `NSWindow` *reports* `sRGB IEC61966-2.1`
already, and the layer was composited unmatched all the same: its sRGB bytes taken as the display's
own space, P3 on the built-in display of every current Mac, so every red was drawn a gamut wider
than it was made for. Converting Blender's captured fur from Display P3 to sRGB reproduces
Bernini's captured fur to within 0.006 per channel, which is the whole of the symptom. Tagging the
`CAMetalLayer`'s `colorspace` sRGB changed nothing, on the editor and on a standalone window alike:
macOS already treats an untagged sRGB-format layer as sRGB.

**Fixed by** `[window setColorSpace:[NSColorSpace sRGBColorSpace]]` in
[`MetalSurface_mac.mm`](../apps/editor/src/Platform/MetalSurface_mac.mm), which is the caller's to
do: `RenderTargetDesc::wnd` is the caller's layer, in the caller's window. After it the viewport's
fur lands on Blender's to within 0.003 per channel in every luma bin.

**Gates.** None a test can hold — the compositor is not in a headless run. Eyes: the material editor
beside Blender's Material Preview on a P3 display, fur the same red.

**If it comes back.** Check that the window's colour space is still set by hand -- a toolkit that
recreates the window, or a viewport created before its widget has one (`[nsView window]` is nil
then, and the set is skipped), loses it. Already ruled out, by measurement, and not worth a second
day: the tone map (a grey sphere matches Eevee to 0.001 and flat greys to 0.009 at every albedo), the
texture bake (byte-for-byte the source, mips averaged in linear light), Eevee's specular (a Diffuse
BSDF renders identically at the glb's specular factor of zero), and Blender's film filter (0.005 luma
over an aligned mask, with a point spread of nearly a one-pixel box).

---

## SIGSEGV in libobjc while a Metal `Graphics` is torn down

**Symptom.** `editor_tests` on `macos-clang-metal-debug` exits 11 with no failing case named, roughly
one suite run in six, always after the last assertion has passed. The crash log
(`build/<preset>/bin/editor_tests_crash_<stamp>.log`) reads:

```
--- CRASH DETECTED (signal 11, faulting address 0x0000000000000000,
                    0x000000018fd5ec4c at /usr/lib/libobjc.A.dylib) ---
```

**The faulting address is `0` and the instruction is in libobjc**, below `-[AGX… dealloc]`, below
`bgl::Graphics::~Graphics`. Two different release chains reached it, so match on that shape rather
than on the frames: one drained an autorelease pool into a released device, the other released an
`MTL::Buffer` out of `~ResourceManager`.

**Cause.** Two defects, both in `CommandQueue::Flush`, both about a Metal object outliving what it
needs:

1. `MTL::CommandQueue::commandBuffer()` returns an **autoreleased** buffer, and `Flush` was the one
   `NewCommandBuffer` caller that scoped no pool around it. Every flush therefore parked a live
   command buffer in the pool `Graphics` held for the whole process — and that pool was declared
   *above* the device, so it drained after the device was released. A command buffer holds its
   queue, which holds the device.
2. `Flush` waited on the event its buffer signals, and that signal fires as the **GPU** passes it,
   with the driver still retiring the buffer and releasing what it held. Measured on an M-series
   machine: after `Flush` returned, 144 of 200 already-executed buffers were still not `Completed`.
   Every caller — `~RenderContext`, `Resize`, `SetRenderScale`, `WaitIdle` — then frees resources
   with `deferred = false`, whose precondition is that the GPU is idle for them
   ([rhi.md](rhi.md) § IResourceManager).

**Fixed by** `Flush` owning the pool its buffer drains into and ending on `waitUntilCompleted`, and
by `Graphics::m_Pool` being declared below the device. The two rules that came out of it are in
[`libs/bgl_extended/CLAUDE.md`](../libs/bgl_extended/CLAUDE.md) § bgl_metal, which is where to read them before writing
Metal code — not here.

**Gates.** `just run bgl_extended_tests -- "[teardown]"`. Two cases, both deterministic where the crash is
not, and each failed before its fix: the queue's retain count must not scale with the number of
flushes (131 retains after 128 flushes, unfixed), and nothing executed before a `Flush` may still be
unretired when it returns (31 of 64, unfixed).

**If it comes back.** Run the gates first. If they pass, this entry's causes are excluded and the
following were already ruled out, so do not spend the day on them again:

- **Metal API validation is clean.** `METAL_DEVICE_WRAPPER_TYPE=1 MTL_DEBUG_LAYER=1` over the whole
  suite reports nothing, so it is not a resource freed while an in-flight command buffer references
  it, and not an over-release Metal tracks.
- **Nothing autoreleases without a pool** on the teardown path — twenty suite runs logged no
  `autoreleased with no pool in place`.
- **It is not the Qt thread hop.** `Renderer` builds *and* tears down its `Graphics` inside `Invoke`,
  so one thread pushes and drains every pool involved.
- **`slot_vector` cannot double-release.** `reclaim_slot` assigns `T()` and throws on a second
  reclaim, and a deferred free's gate covers the frame being recorded when it was retired.

**Reproducing it costs more than you expect.** Nothing smaller than the suite has ever reproduced it:
120 shard runs, 60 runs of the case that crashed, and a 30-vs-30 A/B against the unfixed `libbgl_extended` all
came back clean, as did Metal API validation. It appears to need the four shards running at once,
each holding a device — which is what `just test editor` does and a lone binary does not. Budget for
a rate near one suite run in twenty, and do not read a handful of clean runs as a fix.

---

## Coming back to the editor finds the Material or Animation panel emptied

**Symptom.** The Material Editor holds a mesh, its per-submesh graphs and some unsaved edits to
them. Minimize the editor, restore it, and the panel is back to the default sphere with the graphs
blank — the same for the Animation panel's rig. Nothing warns, no dialog asks, and the held-open
assets have been released, so the Content Explorer will now delete a mesh the panel was showing a
moment ago. Reported as "alt-tab deletes the current mesh"; the app switch is not what does it.

**Cause.** `MainWindow` cleared both panels off `QDockWidget::visibilityChanged(false)`, on the
premise that the signal follows the tab. It follows two things. Measured on Qt 6.8.3 (cocoa): a tab
switch emits `false` for the leaving dock with the window still showing, and **minimizing emits
`false` for every dock** — `isVisible()` still true, `isMinimized()` true — as does hiding the
window, with `isVisible()` false. A plain app switch (Cmd-Tab) and Cmd+H emit nothing at all, with
or without a native child view, which is why the reported gesture never matched the code being read.

**Fixed by** `editor::IsPanelShown` ([panel_visibility.cpp](../apps/editor/src/util/panel_visibility.cpp)),
which holds a panel shown while its window is minimized or hidden, with both destructive
connections in `MainWindow::Build` routed through it. Rendering and the animation clock still follow
raw visibility: parking those while the window is away is right, and reverses itself on restore.

**Gates.** `just run editor_tests -- "[panelclear]"`. The truth table, plus a headless editor holding
`apples.bmesh` that is minimized, restored and hidden and must still name that mesh — and must still
drop it when the tab is left. Three of its assertions fail against the unfixed wiring.

**If it comes back.** The likely arrival is a *fourth* panel wired straight to
`QDockWidget::visibilityChanged`, since only the two connections are guarded, not the signal. Check
that first; `dock->isHidden()` is not the discriminator to reach for instead, because a tab switch
does not hide the unselected dock — Qt moves it off-screen, so it reads unhidden either way.

## Scrolling a panel's properties column smears the main tab bar

**Symptom.** Scroll the Animation panel's left column and a second copy of the window's tab strip —
*Level Editor | Material Editor | Animation Editor* — appears below the real one, offset by roughly
the scroll delta. The duplicate is stale pixels, not a live widget: it does not respond to clicks and
the next full repaint of that region clears it. It shows up wherever a viewport shares a top-level
with a scrolling column, so the Animation panel is where it was found rather than where it lives.

**Cause.** A `QScrollArea` scrolls by **blitting the top-level's backing store** and repainting only
the strip that was exposed. `RenderTargetWindow` takes `Qt::WA_PaintOnScreen`
([RenderTargetWindow.cpp](../apps/editor/src/Windows/RenderTarget/RenderTargetWindow.cpp)), which
implies `WA_NativeWindow` and realises a native view through `winId()` — deliberately, since the
swapchain needs a real surface. Qt does not composite that widget through the backing store, so the
store's idea of the window disagrees with what is on screen, and the blit is computed against the
wrong geometry. The give-away is *where* the smear lands: the tab strip is not inside the scroll
area at all, so the blit wrote outside the widget that issued it. That also rules out the obvious
guess — forcing `viewport()->update()` on scroll repaints the viewport and cannot clean a region
outside it.

The bug predates the Animation panel's blend-space editor; that work only added enough controls to
the column to make it scroll in an ordinary window, which is why it surfaced then.

**Fixed by** giving the scroll area's viewport a surface of its own —
`scrollBox->viewport()->setAttribute(Qt::WA_NativeWindow)` in
`AnimationEditorWindow::BuildPropertiesColumn`. A native viewport cannot blit past itself, whatever
the top-level's store believes. The cost is one extra native view per scrolling column.

**Gates.** None, and that is the honest state of it: the artifact is stale pixels in a compositor
surface, which nothing the suite can assert reaches — an offscreen render is composited correctly and
shows nothing. It is checked by eye, by scrolling the column with a viewport on screen.

**If it comes back.** Suspect a *new* scrolling column that did not get the attribute, rather than a
regression in this one — the fix is per scroll area and nothing enforces it. `WA_PaintOnScreen` on
the viewport is not the remedy to reach for instead: that stops the widget being composited at all,
which is what caused this in the first place.
---

## Flat skin-coloured patches over a blended, double-sided character

**Symptom.** A closed mesh — a head, a body — set to the Alpha Blend layer with Double Sided on
draws unshaded, flat-toned patches over its front: mouth and nostril interiors compositing over the
face, moving with the camera. Reported first from the Material Editor's Layer controls, on a
material whose base colour carries no alpha channel at all.

**Cause.** Not a defect, and not this feature's: the transparent phase sorts instances, never the
triangles inside one, and writes no depth among them — so within one mesh the last-rasterized
triangle wins. Double Sided is what lets the interior faces reach the rasterizer, and their flipped
normals are why they shade flat. With no alpha channel every fragment lands at coverage 1, so the
whole head pays the transparent path's ordering hazards for an image Opaque would draw correctly.
The full mechanism is [passes.md § Two-sided surfaces](passes.md) — "it is the geometry showing
through, not a defect in the sort". The engine's own PBR blend materials do exactly the same; a
game-defined surface inherits it from the shared blend bucket.

**The answer** is a material choice, not a fix: Double Sided off where a translucent solid has no
inside worth drawing, or the Hashed Alpha layer, which writes real depth and self-occludes (and
needs TAA running). Alpha Blend earns its keep only when something feeds alpha below 1.

**Gates.** `just run bgl_extended_tests -- "[twosided]"` pins the facing and the mesh-stage cull the
paragraph above rests on. There is no gate that could pin per-triangle sorting, because the engine
deliberately has none.

**If it comes back.** It never left; this entry exists so the symptom is recognised as the blend
bucket's documented behaviour rather than diagnosed as a regression of whatever feature last
touched the material path.
