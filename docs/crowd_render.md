# crowd_render

A crowdlib crowd drawn by the renderer GPU to GPU. `crowd_render::CrowdInstanceBlocks` places
every agent of a crowd in one view each frame from the crowd's render ring, with no per-agent work
on the CPU. Both halves are other libraries' public contracts: the crowd's render ring
([crowdlib.md](crowdlib.md), § A render ring for a reader on another queue) and the renderer's
instance blocks and writers ([bgl_api.md](bgl_api.md)). This library is the glue between them.

## Why a library of its own

`crowdlib` never links the renderer, and `bgl` knows no crowd. The glue links both, so it cannot
live in either. It cannot live in `gamelib` either: the editor links `gamelib` through
`editor_plugin_api`, and the editor never links `crowdlib`. So it is `libs/crowd_render`, static,
holding no process-wide state. Anything that draws a crowd links it: a game, `bgl_crowd`.

## What a frame does

```cpp
auto blocks = crowd_render::CrowdInstanceBlocks(
    crowd_render::CrowdInstanceBlocksDesc()
        .SetCrowd(crowd)          // created with CrowdDesc::renderRingTicks
        .SetGraphics(graphics)    // on the crowd's GPU context
        .SetView(view)
        .AddType(crowd_render::AgentTypeMeshDesc().AddGeom(infantry).SetCapacity(4000).SetModel(lift))
        .AddType(crowd_render::AgentTypeMeshDesc().AddGeom(cavalry).SetCapacity(500).SetModel(lift)));

// every frame
blocks.PrepareFrame(accumulator / tickSeconds);
graphics->DrawFrame(target, job);
blocks.FinishFrame();
```

- **One block per geom of an agent type**, all placed alike. Slot `i` of a type's blocks is record
  `i` of the type's run in the drawn tick (the crowd groups each tick's records by type), and the slots past
  the run are hidden. A block draws and culls every one of its `capacity` placements each frame,
  live or not, so `AgentTypeMeshDesc::capacity` is the most agents of that type the game holds at
  once: the crowd's `maxAgents` when it is left 0, and agents past it go undrawn.
- **A type is a list of geoms, and may be skinned.** `AgentTypeMeshDesc::geoms` is what draws an
  agent: a character cooked as several meshes is several geoms, all static or all skinned. A
  skinned type's blocks are the renderer's skinned blocks, on the automatic pose source, sharing
  the type's `playback`; each agent plays it ahead of the clock by its `RenderAgent::id`'s share of
  `phaseSpreadSeconds` -- the id times the golden ratio's fraction of 2^32, wrapped -- so neighbours
  never step together and an agent keeps its phase across a split, a merge or a destroy. A
  looping clip's cycle is the spread that uses every phase; 0 plays the type in step. Each geom is
  a placement of its own to the renderer, so a character's meshes are posed apart when near, as
  the same character spawned from the CPU is.
- **The frame draws between the last two completed ticks.** `PrepareFrame(alpha)` points every
  block at the latest completed tick `c`, `alpha` of the way from `c − 1` ("Fix Your Timestep"),
  so the crowd is drawn about a tick late (≈33 ms at the default tick). It also inserts a wait on
  the tick's queue point (`IGraphics::WaitBeforeNextFrame`). The tick has already completed, so the
  wait passes at once; it is what orders the crowd's writes before the frame's reads on another
  queue.
- **Motion vectors follow the agent, not the slot.** A slot's `prevTransform` is last frame's pose
  of the same agent. If last frame drew the same tick, it is the same slot at last frame's alpha.
  If it drew the tick before, it is the slot `RenderAgent::source` names, a tick further back. A
  split, a merge or a destroy moves an agent to another index between ticks, and following `source`
  keeps its motion right across it. A spawned agent, and every agent after a frame that skipped a
  tick, has prev = current: no motion, rather than a wrong one.
- **`FinishFrame` hands ticks back.** A frame reads `c`, `c − 1` and `c − 2`, and `c` only grows.
  So once a frame has ended, nothing will read `c − 3` again, and `FinishFrame` releases it at
  `IGraphics::GetLastFrameDone()`. The crowd's `Step` waits for that point on its own queue before
  overwriting the tick. Without the releases the crowd stops stepping once its ring is full, which
  is also what happens when the view stops being drawn.

The writers are `CrowdInstanceWriter` and `SkinnedCrowdInstanceWriter` in
`crowd_render.CrowdInstanceWriter` (`libs/crowd_render/shaders/src/crowd_render/CrowdInstanceWriter.slang`),
sharing their placement code and one `CrowdWriterParams`; each is compiled once by
`IGraphics::CreateMeshInstanceWriter`, and only when some type is of its kind. A type's mesh stands in the agent's frame: +z its facing, y
up, the origin on the ground where it stands. `AgentTypeMeshDesc::model` is the geom's placement in
that frame.

## Teardown

Release the blocks first: their destructor deletes the blocks from the view. The blocks hold
references to the crowd, the renderer and the view, so those outlive them anyway. The renderer's
import of the ring holds a native reference of its own, so the ring's memory outlives the crowd
for as long as a frame may still read it.

## Tests

`libs/crowd_render/tests`. `CrowdInstanceWriter_test.cpp` runs both writers against a recording
block (`tests/shaders/RecordingCrowdBlock.slang`), with the parameters the library plans
(`src/WriterFrame.h`), over a GPU crowd's real ring. It checks that every agent's prevTransform is
where the previous frame placed it across a split, a destroy, a merge and a spawn, that a frame
between ticks places each agent `alpha` of the way, that the static writer gives no offset, and
that the skinned one gives each agent its id's share of the spread for its life, no two
neighbours alike. `CrowdInstanceBlocks_test.cpp` drives the class against a real renderer: what it
refuses, a block per geom, and that frames bracketed by it keep the crowd stepping past its ring.
A skinned crowd drawn by the real renderer is `bgl_crowd`'s (`--project`, `--import`).
