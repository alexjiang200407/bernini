# The IDL modules are Slang sources in the renderer's shader tree, where the shaders import them as
# idl.<Name>; nothing is copied. What is generated is the C++ mirror, into the build tree, because a
# struct's layout follows the backend it is generated for, so a committed copy would be right for one
# and silently wrong for the other.
set(IDL_SRC_ROOT           "${BERNINI_ROOT}/libs/bgl/shaders/src/idl")
# The offset primitives (Entry, Range ...) and ErrorCode are bgpu's: a module here imports them as
# siblings, which they are once staged, so the generator searches both.
set(IDL_BGPU_ROOT          "${BERNINI_ROOT}/libs/bgpu/shaders/src/idl")
set(IDL_CPP_OUT_ROOT       "${CMAKE_BINARY_DIR}/generated/bgl_idl")
set(IDL_CPP_OUT_DIR        "${IDL_CPP_OUT_ROOT}/bgl/idl")
# The public headers stay in the source tree, and stay committed: a consumer includes <bgl/...>
# without building the renderer. Safe because --public refuses a struct whose size or field offsets differ
# between MSL and the C/C++ rules, and emits no alignas, so every backend generates the same bytes.
set(IDL_PUBLIC_CPP_OUT_DIR "${BERNINI_ROOT}/libs/bgl/include/bgl")

file(GLOB_RECURSE IDL_SOURCES CONFIGURE_DEPENDS "${IDL_SRC_ROOT}/*.slang")

# Every module that emits a C++ mirror must be named in exactly one of the two lists below --
# CMake declares the .h as an OUTPUT, so it cannot glob for it. `scripts/gen_idl.py` (`just idl`)
# parses these same two lists, so the two generation paths cannot drift and emit different trees.
#
# A module in NEITHER list is a shader-only module and nothing is generated for it. That is correct
# for the interface- and generic-only modules -- IMaterial, IPbrMaterial, IAlphaTestMaterial, and the
# `Entry<T>` / `Range<T>` / `RangeWithCount<T>` / `RawEntry<T>` offset primitives. A generic has no
# concrete layout to mirror, so idlgen skips it (and says so); their C++ counterparts in
# bgpu's include/bgpu/idl are **hand-written** and carry no generated banner. Listing one here would
# declare a CMake OUTPUT nothing produces.

# Internal: namespace `bgl::idl`, emitted into IDL_CPP_OUT_DIR (the build tree).
set(IDL_CPP_SOURCES
    Meshlet.slang
    MeshletGroup.slang
    VertexLayout.slang
    Submesh.slang
    Geom.slang
    CullView.slang
    CullStats.slang
    InstanceVisibility.slang
    InstanceLod.slang
    LodSubmeshRange.slang
    MeshInstance.slang
    Constants.slang
    Clip.slang
    ClipPlayback.slang
    PbrMaterial.slang
    LoosePbrMaterial.slang
    GameSurfaceRecord.slang
    RawTextureHandle.slang
    DrawBucket.slang
    DispatchArgs.slang
    BaseTable.slang
    LodDrawMode.slang
    BoneSample.slang
    SkinnedBone.slang
    SkinnedLegChain.slang
    Ramp.slang
    FootIKLeg.slang
    PosedInstance.slang
    BlobShadow.slang
    GrassLook.slang
    GrassChunk.slang
    GrassClump.slang
    GrassDraw.slang
    GrassChunkRef.slang
    Rig.slang
    BlendNodeKind.slang
    BlendSpaceSample.slang
    BlendNode.slang
    BlendSlot.slang
    SkinnedState.slang
    SkinnedTableState.slang
    SkinnedAutoState.slang
    InstancePose.slang
    PosePool.slang
    PlaybackType.slang
)

# Public engine API: namespace `bgl`, emitted into include/bgl. MaterialType is named by a public
# header (MaterialHandle::materialType), so it cannot live behind `bgl::idl`. There is deliberately
# no `bgl::idl` copy of it -- a second, differently-namespaced definition of the same enum is a
# duplicate waiting to be included by mistake.
#
# Keep this list to what a *client* names. An enum only bgl's own passes switch on belongs above,
# however public its consumers feel: a renderer's pipeline permutations are not vocabulary a public
# interface should carry.
set(IDL_PUBLIC_CPP_SOURCES
    MaterialType.slang
    MeshInstanceFlag.slang
    LodLevel.slang
    OverlayVertex.slang
)

# The C++ mirror follows the backend's own rules; see IDL_CPP_OUT_DIR above.
if(RENDERER_BACKEND STREQUAL "METAL")
    set(IDL_LAYOUT_ARGS --metal-layout)
else()
    set(IDL_LAYOUT_ARGS "")
endif()

set(IDL_GENERATED_OUTPUTS "")

foreach(src ${IDL_SOURCES})
    file(RELATIVE_PATH rel "${IDL_SRC_ROOT}" "${src}")
    get_filename_component(rel_dir "${rel}" DIRECTORY)
    get_filename_component(name "${src}" NAME_WE)
    get_filename_component(file "${src}" NAME)

    if(rel_dir)
        set(rel_stem "${rel_dir}/${name}")
    else()
        set(rel_stem "${name}")
    endif()

    if(file IN_LIST IDL_PUBLIC_CPP_SOURCES)
        # Public engine header: namespace `bgl`, emitted into include/bgl. --public makes idlgen
        # refuse a struct that does not lay out the same on every backend, because this one is
        # committed and they all compile against it.
        set(cpp_args --cpp-out-dir "${IDL_PUBLIC_CPP_OUT_DIR}" --namespace "bgl" --public)
        set(output "${IDL_PUBLIC_CPP_OUT_DIR}/${rel_stem}.h")
    elseif(file IN_LIST IDL_CPP_SOURCES)
        set(cpp_args --cpp-out-dir "${IDL_CPP_OUT_DIR}")
        set(output "${IDL_CPP_OUT_DIR}/${rel_stem}.h")
    else()
        continue()
    endif()

    # A module's layout depends on the final size of every struct it embeds, so a change to an
    # imported module has to regenerate this one too.
    set(import_deps "")
    file(STRINGS "${src}" import_lines REGEX "^[ \t]*import[ \t]+[A-Za-z0-9_.]+[ \t]*;")
    foreach(import_line IN LISTS import_lines)
        string(REGEX REPLACE "^[ \t]*import[ \t]+([A-Za-z0-9_.]+)[ \t]*;.*$" "\\1" imported "${import_line}")
        string(REPLACE "." "/" imported_path "${imported}")
        if(NOT "${imported_path}" STREQUAL "${rel_stem}")
            if(EXISTS "${IDL_SRC_ROOT}/${imported_path}.slang")
                list(APPEND import_deps "${IDL_SRC_ROOT}/${imported_path}.slang")
            else()
                list(APPEND import_deps "${IDL_BGPU_ROOT}/${imported_path}.slang")
            endif()
        endif()
    endforeach()

    add_custom_command(
        OUTPUT  "${output}"
        COMMAND bgpu_idlgen
                --src-root "${IDL_SRC_ROOT}"
                ${IDL_LAYOUT_ARGS}
                ${cpp_args}
                -I "${IDL_SRC_ROOT}"
                --extern "${IDL_BGPU_ROOT}=bgpu::idl"
                "${src}"
        DEPENDS bgpu_idlgen "${src}" ${import_deps}
        COMMENT "idlgen ${rel}"
        VERBATIM
    )

    list(APPEND IDL_GENERATED_OUTPUTS "${output}")
endforeach()

add_custom_target(bgl_idl_generate ALL DEPENDS ${IDL_GENERATED_OUTPUTS})

# The internal mirrors, <bgl/idl/<Module>.h>: the renderer's and nobody else's, reached by what links
# the renderer's objects. The second root because a generated header includes its imports as
# siblings ("Geom.h").
add_library(bgl_idl INTERFACE)
target_include_directories(bgl_idl INTERFACE ${IDL_CPP_OUT_ROOT} ${IDL_CPP_OUT_DIR})
target_link_libraries(bgl_idl INTERFACE bgpu)
add_dependencies(bgl_idl bgl_idl_generate)
