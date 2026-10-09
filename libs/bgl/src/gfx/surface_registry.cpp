#include "gfx/surface_registry.h"

#include "gfx/DrawBucketTable.h"
#include "gfx/SurfaceReflection.h"
#include "passes/draw_bucket_config.h"
#include "util/util.h"
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/idl/DrawBucket.h>
#include <bgl/types/LayerType.h>
#include <bgpu/GpuContext.h>
#include <bgpu/device/Device.h>
#include <core/err/util.h>
#include <core/log/log.h>
#include <slang.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
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

		/**
		 * The surface `moduleName` declares, read through the context's compiler and so at the
		 * offsets this backend will read a record at. Empty when the module does not import the
		 * contract. Loaded and reflected in one call, because a slang::IModule only lives as long as
		 * the session that parsed it, and the next AddSourceModule drops that.
		 *
		 * @throws std::runtime_error if the module does not compile, or imports the contract and
		 *         declares no single surface.
		 */
		std::optional<ReflectedSurface>
		ReflectSurfaceModule(bgpu::GpuContext& context, const std::string& moduleName)
		{
			std::string     diagnostic;
			slang::IModule* slangModule = context.LoadScalarLayoutModule(moduleName, diagnostic);
			if (slangModule == nullptr)
			{
				core::throw_runtime_error(
					"surface '{}': its module did not compile\n{}",
					moduleName,
					diagnostic);
			}

			return ReflectSurface(slangModule, moduleName);
		}

		constexpr uint32_t c_MaxSurfaces = idl::cMaxDrawBuckets - 1;

		// What the slot's programs import: the surface's type under a name of the slot's own, so two
		// surfaces declaring the same struct name never meet in one program.
		std::string
		BindingModuleName(uint32_t slot)
		{
			return std::format("game.slot{}", slot);
		}

		// Every contract but ISurfaceSource draws through the lit programs: a toon surface does so
		// as its model's adapter (lib.math.ToonShading), whose Shade is the engine's toon lighting.
		bool
		DrawsLitPrograms(SurfaceShading shading) noexcept
		{
			switch (shading)
			{
			case SurfaceShading::kPbrSurface:
				return false;
			case SurfaceShading::kLit:
			case SurfaceShading::kToonCharacter:
				return true;
			}
			return false;
		}

		std::string
		BindingModuleSource(
			uint32_t           slot,
			const std::string& module,
			const std::string& sourceType,
			SurfaceShading     shading)
		{
			if (shading != SurfaceShading::kToonCharacter)
			{
				return std::format(
					"import {};\npublic typealias Slot{}Surface = {};\n",
					module,
					slot,
					sourceType);
			}
			// A character's programs at rest name the game's type itself, to shade it with the toon
			// shading rig its placement carries; see ToonColorProgramSource.
			return std::format(
				"import {0};\nimport lib.math.ToonShading;\npublic typealias Slot{1}Surface = "
				"ToonCharacterLit<{2}>;\npublic typealias Slot{1}Source = {2};\n",
				module,
				slot,
				sourceType);
		}

		// A registered surface's programs, generated rather than shipped because a program has to
		// name the surface's type: PSMain, and PSDissolve for the bucket's dissolve lane.
		std::string
		ColorProgramSource(uint32_t slot, std::string_view program)
		{
			return std::format(
				"import {0};\nimport lib.forward.GameSurface;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\nimport "
				"lib.forward.lod_dissolve;\n\n[shader(\"pixel\")]\n"
				"ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)\n{{\n"
				"    return materialData.{1}<Slot{2}Surface>(input, isFrontFace);\n}}\n\n"
				"[shader(\"pixel\")]\n"
				"ForwardPSOut PSDissolve(DissolveVSOut input, bool isFrontFace: "
				"SV_IsFrontFace)\n{{\n"
				"    DiscardDissolvedLod(input);\n"
				"    return MarkDissolvedLod(materialData.{1}<Slot{2}Surface>(input.Surface(), "
				"isFrontFace));\n}}\n",
				BindingModuleName(slot),
				program,
				slot);
		}

		// A toon character surface's programs, named on the game's type so they light it with the toon
		// sun: PSMain reads ToonVSOut, which carries the placement's toon shading rig block; the
		// dissolve lane's vertices carry none.
		std::string
		ToonColorProgramSource(uint32_t slot, std::string_view program)
		{
			return std::format(
				"import {0};\nimport lib.forward.GameSurface;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\nimport "
				"lib.forward.lod_dissolve;\n\n[shader(\"pixel\")]\n"
				"ForwardPSOut PSMain(ToonVSOut input, bool isFrontFace: SV_IsFrontFace)\n{{\n"
				"    return materialData.{1}<Slot{2}Source>(input.Surface(), "
				"input.toonShadingRigSlot, "
				"isFrontFace);\n}}\n\n"
				"[shader(\"pixel\")]\n"
				"ForwardPSOut PSDissolve(DissolveVSOut input, bool isFrontFace: "
				"SV_IsFrontFace)\n{{\n"
				"    DiscardDissolvedLod(input);\n"
				"    return MarkDissolvedLod(materialData.{1}<Slot{2}Source>(input.Surface(), 0u, "
				"isFrontFace));\n}}\n",
				BindingModuleName(slot),
				program,
				slot);
		}

		// A surface's grass program: the blade's vertex in, and the surface shaded on it.
		std::string
		GrassProgramSource(uint32_t slot, std::string_view program)
		{
			return std::format(
				"import {};\nimport lib.forward.GrassShading;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\nimport lib.forward.grass_vertex;\n\n"
				"[shader(\"pixel\")]\n"
				"GrassPSOut PSMain(GrassVSOut input)\n{{\n"
				"    return materialData.{}<Slot{}Surface>(input);\n}}\n",
				BindingModuleName(slot),
				program,
				slot);
		}

		// A surface's albedo as the Ground Color pass reads it off a terrain drawn through it.
		std::string
		GroundColorProgramSource(uint32_t slot, std::string_view program)
		{
			return std::format(
				"import {};\nimport lib.forward.GroundColor;\nimport lib.forward.MaterialData;\n"
				"import lib.forward.common;\n\n"
				"[shader(\"pixel\")]\n"
				"GroundColorOut PSMain(ForwardVSOut input)\n{{\n"
				"    return materialData.{}<Slot{}Surface>(input);\n}}\n",
				BindingModuleName(slot),
				program,
				slot);
		}

		// The shared blend program, with one arm per registered surface ahead of the engine's own
		// kinds -- the arm's function picked by the surface's contract; it shadows
		// programs/forward/Transparent.slang, which is this with no arms.
		std::string
		TransparentProgramSource(const std::span<const SurfaceType> types)
		{
			std::string imports;
			std::string arms;
			for (uint32_t slot = 0; slot < types.size(); ++slot)
			{
				imports += std::format("import {};\n", BindingModuleName(slot));
				const bool toon = types[slot].shading == SurfaceShading::kToonCharacter;
				arms += std::format(
					"    case {}u:\n        return "
					"materialData.{}<Slot{}{}>(input, "
					"isFrontFace);\n",
					std::to_underlying(types[slot].kind),
					toon                                  ? "ShadeGameToonBlended" :
					DrawsLitPrograms(types[slot].shading) ? "ShadeGameLitBlended" :
															"ShadeGameBlended",
					slot,
					toon ? "Source" : "Surface");
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
		// program, a grass one and a terrain's ground colour -- the lit family where the surface owns
		// its lighting. Named by the draw-bucket
		// config, so the names generated here are the names the passes build. A toon character has no
		// grass program: a grass look refuses one.
		std::vector<bgpu::SlangSourceModule>
		SurfacePrograms(uint32_t slot, MaterialType kind, SurfaceShading shading)
		{
			const auto colour = [kind](LayerType layer) {
				return DrawBucketPixelSrc(
					DrawBucketDesc{ GeometryStage::kStaticMesh, kind, layer });
			};

			if (shading == SurfaceShading::kToonCharacter)
			{
				return {
					{ colour(LayerType::kOpaque),
					  ToonColorProgramSource(slot, "GameToonOpaqueProgram"),
					  false },
					{ colour(LayerType::kMask),
					  ToonColorProgramSource(slot, "GameToonAlphaTestedProgram"),
					  false },
					{ colour(LayerType::kHashed),
					  ToonColorProgramSource(slot, "GameToonHashedAlphaProgram"),
					  false },
				};
			}

			// Entry programs nothing imports, so each loads only when a draw bucket builds it.
			const bool lit = DrawsLitPrograms(shading);
			return {
				{ colour(LayerType::kOpaque),
				  ColorProgramSource(slot, lit ? "GameLitOpaqueProgram" : "GameOpaqueProgram"),
				  false },
				{ colour(LayerType::kMask),
				  ColorProgramSource(
					  slot,
					  lit ? "GameLitAlphaTestedProgram" : "GameAlphaTestedProgram"),
				  false },
				{ colour(LayerType::kHashed),
				  ColorProgramSource(
					  slot,
					  lit ? "GameLitHashedAlphaProgram" : "GameHashedAlphaProgram"),
				  false },
				{ DrawBucketPixelSrc(
					  DrawBucketDesc{ GeometryStage::kGrass, kind, LayerType::kOpaque }),
				  GrassProgramSource(slot, lit ? "GameLitGrassProgram" : "GameGrassProgram"),
				  false },
				{ DrawBucketGroundColorSrc(
					  DrawBucketDesc{ GeometryStage::kTerrain, kind, LayerType::kOpaque }),
				  GroundColorProgramSource(
					  slot,
					  lit ? "GameLitGroundColorProgram" : "GameGroundColorProgram"),
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
	RegisterSurfaces(bgpu::IDevice& device, const std::filesystem::path& dir)
	{
		if (dir.empty())
			return {};

		if (!std::filesystem::is_directory(dir))
		{
			throw ApiError(
				std::format("clientShaderDir '{}' is not a directory", dir.generic_string()));
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
				spdlog::debug("surface directory: skipping '{}', not an importable name", stem);
				continue;
			}

			std::optional<ReflectedSurface> reflected;
			try
			{
				reflected = ReflectSurfaceModule(device.GetGpuContext(), stem);
			}
			catch (const std::exception& e)
			{
				// The reflection throws a plain error; this is the seam where a bad module becomes
				// one the client catches.
				throw ApiError(e.what());
			}

			// A module that never imported the contract is the game's own code sitting in its own
			// shader directory, not a surface that failed to be one.
			if (!reflected.has_value())
			{
				spdlog::debug("surface directory: '{}' declares no surface, skipping", stem);
				continue;
			}

			// Past this not even one draw bucket each could exist beside the unlit fallback's.
			if (types.size() == c_MaxSurfaces)
			{
				throw ApiError(
					std::format(
						"clientShaderDir '{}' holds more than {} surfaces, the most the "
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
			      BindingModuleSource(
					  slot,
					  types[slot].surfaceName,
					  sourceTypes[slot],
					  types[slot].shading) });

			for (const bgpu::SlangSourceModule& program :
			     SurfacePrograms(slot, types[slot].kind, types[slot].shading))
			{
				device.AddSourceModule(program);
			}

			spdlog::info(
				"surface '{}' registered into game slot {}",
				types[slot].surfaceName,
				slot);
		}

		if (!types.empty())
		{
			device.AddSourceModule(
				{ "programs.forward.Transparent", TransparentProgramSource(types), false });
		}

		return types;
	}
}
