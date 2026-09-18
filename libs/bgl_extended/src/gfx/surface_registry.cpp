#include "gfx/surface_registry.h"

#include "device/Device.h"
#include "gfx/DrawBucketTable.h"
#include "passes/draw_bucket_config.h"
#include "slang/SlangSessions.h"
#include "util/util.h"
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl_common/SurfaceReflection.h>
#include <bgl_common/idl/DrawBucket.h>
#include <core/log/log.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		// The stem becomes an `import` in a module the engine writes, so it has to be spellable
		// there. A file the client can name and the shader cannot is the one refusal that would
		// otherwise surface as a compile error inside generated text nobody wrote.
		bool
		IsImportableName(const std::string& stem) noexcept
		{
			if (stem.empty() || (std::isdigit(static_cast<unsigned char>(stem.front())) != 0))
				return false;

			return std::ranges::all_of(stem, [](char c) {
				return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
			});
		}

		constexpr uint32_t c_MaxSurfaces = idl::cMaxDrawBuckets - 1;

		// What the slot's programs import: the surface's type under a name of the slot's own, so two
		// surfaces declaring the same struct name never meet in one program.
		std::string
		BindingModuleName(uint32_t slot)
		{
			return std::format("game.slot{}", slot);
		}

		std::string
		BindingModuleSource(uint32_t slot, const std::string& module, const std::string& sourceType)
		{
			return std::format(
				"import {};\npublic typealias Slot{}Surface = {};\n",
				module,
				slot,
				sourceType);
		}

		// A registered surface's programs, generated rather than shipped because a program has to
		// name the surface's type.
		std::string
		ColorProgramSource(uint32_t slot, std::string_view program)
		{
			return std::format(
				"import {};\nimport lib.forward.GameSurface;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\n\n[shader(\"pixel\")]\n"
				"ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)\n{{\n"
				"    return materialData.{}<Slot{}Surface>(input, isFrontFace);\n}}\n",
				BindingModuleName(slot),
				program,
				slot);
		}

		std::string
		CoverageProgramSource(uint32_t slot, std::string_view discard)
		{
			return std::format(
				"import {};\nimport lib.forward.GameSurface;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\n\n[shader(\"pixel\")]\n"
				"void PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)\n{{\n"
				"    materialData.{}<Slot{}Surface>(input, isFrontFace);\n}}\n",
				BindingModuleName(slot),
				discard,
				slot);
		}

		// The shared blend program, with one arm per registered surface ahead of the engine's own
		// kinds; it shadows programs/forward/Transparent.slang, which is this with no arms.
		std::string
		TransparentProgramSource(const std::span<const SurfaceType> types)
		{
			std::string imports;
			std::string arms;
			for (uint32_t slot = 0; slot < types.size(); ++slot)
			{
				imports += std::format("import {};\n", BindingModuleName(slot));
				arms += std::format(
					"    case {}u:\n        return "
					"materialData.ShadeGameBlended<Slot{}Surface>(input, "
					"isFrontFace);\n",
					static_cast<uint32_t>(types[slot].kind),
					slot);
			}

			return std::format(
				"{}import lib.forward.GameSurface;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.MaterialShading;\nimport lib.forward.common;\n\n"
				"[shader(\"pixel\")]\n"
				"float4 PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : "
				"SV_Target\n{{\n"
				"    switch (uint(LoadMaterialKind(input.materialOffset)))\n    {{\n{}"
				"    default:\n        return materialData.ShadeBlendedEngineKind(input, "
				"isFrontFace);\n"
				"    }}\n}}\n",
				imports,
				arms);
		}

		// Every program a surface's draw buckets can ask for: an opaque, alpha-test and hashed colour
		// program, and the static depth pass's coverage twins. Named by the draw-bucket config, so the
		// names generated here are the names the passes build.
		std::vector<SlangSourceModule>
		SurfacePrograms(uint32_t slot, MaterialType kind)
		{
			const auto colour = [kind](LayerType layer) {
				return DrawBucketPixelSrc(DrawBucketDesc{ GeomType::kStaticMesh, kind, layer });
			};
			const auto coverage = [kind](LayerType layer) {
				return DrawBucketCoveragePixelSrc(
					DrawBucketDesc{ GeomType::kStaticMesh, kind, layer });
			};

			// Entry programs nothing imports, so each loads only when a draw bucket builds it.
			return {
				{ colour(LayerType::kOpaque),
				  ColorProgramSource(slot, "GameOpaqueProgram"),
				  false },
				{ colour(LayerType::kMask),
				  ColorProgramSource(slot, "GameAlphaTestedProgram"),
				  false },
				{ colour(LayerType::kHashed),
				  ColorProgramSource(slot, "GameHashedAlphaProgram"),
				  false },
				{ coverage(LayerType::kMask),
				  CoverageProgramSource(slot, "DiscardUncoveredGameAlphaTested"),
				  false },
				{ coverage(LayerType::kHashed),
				  CoverageProgramSource(slot, "DiscardUncoveredGameHashedAlpha"),
				  false },
			};
		}

		std::vector<std::filesystem::path>
		SurfaceFiles(const std::filesystem::path& dir)
		{
			std::vector<std::filesystem::path> files;
			for (const std::filesystem::directory_entry& entry :
			     std::filesystem::directory_iterator(dir))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".slang")
					files.emplace_back(entry.path());
			}

			// Filename order, so a slot is decided by the directory alone and a file added later
			// does not renumber the ones before it.
			std::ranges::sort(files);
			return files;
		}
	}

	std::vector<SurfaceType>
	RegisterSurfaces(IDevice& device, const std::filesystem::path& dir)
	{
		if (dir.empty())
			return {};

		if (!std::filesystem::is_directory(dir))
		{
			throw ApiError(
				std::format("surfaceShaderDir '{}' is not a directory", dir.generic_string()));
		}

		std::vector<SurfaceType> types;
		std::vector<std::string> sourceTypes;

		for (const std::filesystem::path& file : SurfaceFiles(dir))
		{
			const std::string stem = file.stem().string();

			// Nothing could name it, so nothing here can be a surface. The same directory is the
			// game's module search path, and what it calls its own files is its business.
			if (!IsImportableName(stem))
			{
				logger::debug("surface directory: skipping '{}', not an importable name", stem);
				continue;
			}

			std::optional<ReflectedSurface> reflected;
			try
			{
				reflected = device.ReflectSurfaceModule(stem, stem);
			}
			catch (const std::exception& e)
			{
				// The reflection answers to bgl_common, which cannot throw the API's error; this is
				// the seam where a bad module becomes one the client catches.
				throw ApiError(e.what());
			}

			// A module that never imported the contract is the game's own code sitting in its own
			// shader directory, not a surface that failed to be one.
			if (!reflected.has_value())
			{
				logger::debug("surface directory: '{}' declares no surface, skipping", stem);
				continue;
			}

			// Past this not even one draw bucket each could exist beside the unlit fallback's.
			if (types.size() == c_MaxSurfaces)
			{
				throw ApiError(
					std::format(
						"surfaceShaderDir '{}' holds more than {} surfaces, the most the "
						"draw-bucket "
						"ceiling can give a bucket each; '{}' is past the last",
						dir.generic_string(),
						c_MaxSurfaces,
						stem));
			}

			reflected->type.kind = GameSlotKind(static_cast<uint32_t>(types.size()));
			types.emplace_back(std::move(reflected->type));
			sourceTypes.emplace_back(std::move(reflected->sourceTypeName));
		}

		// Only now: a binding drops every session, so reflecting after one would rebuild Slang's
		// core module for each surface after the first.
		for (uint32_t slot = 0; slot < types.size(); ++slot)
		{
			device.AddSourceModule(
				{ BindingModuleName(slot),
			      BindingModuleSource(slot, types[slot].name, sourceTypes[slot]) });

			for (const SlangSourceModule& program : SurfacePrograms(slot, types[slot].kind))
			{
				device.AddSourceModule(program);
			}

			logger::info("surface '{}' registered into game slot {}", types[slot].name, slot);
		}

		if (!types.empty())
		{
			device.AddSourceModule(
				{ "programs.forward.Transparent", TransparentProgramSource(types), false });
		}

		return types;
	}
}
