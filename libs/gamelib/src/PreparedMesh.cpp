#include <gamelib/PreparedMesh.h>

#include <gamelib/vat_freshness.h>

#include <assetlib/banim_io.h>
#include <assetlib/image_io.h>
#include <assetlib/skinning.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <core/err/util.h>

namespace game
{
	namespace
	{
		/**
		 * The mesh read and cooked, its materials read and their textures decoded -- the half every
		 * tier shares. `out.tier` and whatever else the tier needs are the caller's to fill in.
		 *
		 * `mesh` is read, never stored: nothing a commit needs off one survives past this call, so
		 * the parsed container dies on the thread that read it.
		 */
		PreparedMesh
		PrepareCommon(
			const assetlib::AssetStore& store,
			std::string_view            relPath,
			uint32_t                    meshIndex,
			const assetlib::BMesh&      mesh)
		{
			core::throw_runtime_error_if(
				meshIndex >= mesh.meshes.size(),
				"AssetManager: mesh index {} out of range in '{}'",
				meshIndex,
				relPath);

			const assetlib::Mesh& entry = mesh.meshes[meshIndex];

			auto out = PreparedMesh();
			// The raw path, not a normalized one: it is what the acquire keys the shared geom by, and
			// the path-taking acquire keys by what its caller passed.
			out.relPath   = std::string(relPath);
			out.meshIndex = meshIndex;
			out.cooked    = bgl::CookStaticMesh(mesh, meshIndex);

			out.materials.resize(mesh.materials.size());
			out.submeshMaterials.reserve(entry.submeshCount);

			// Only what *this* mesh's submeshes name: another mesh in the same file may name others,
			// and reading those would be a decode nobody asked for.
			auto named = std::vector<bool>(mesh.materials.size(), false);

			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const uint32_t index = mesh.submeshes[entry.firstSubmesh + i].material;
				out.submeshMaterials.push_back(index);

				if (index < mesh.materials.size())
					named[index] = true;
			}

			for (size_t index = 0; index < named.size(); ++index)
			{
				// A slot the source left unrouted. It stays empty, and the acquire leaves every
				// submesh naming it unlit -- which is what an invalid handle means to the scene.
				if (!named[index] || mesh.materials[index].empty())
					continue;

				PreparedMaterial& prepared = out.materials[index];
				prepared.relPath           = mesh.materials[index];
				prepared.source            = store.LoadMaterial(prepared.relPath);
				prepared.loose             = store.DrawsLoose(prepared.source);

				for (const std::string& texture : MaterialTextures(prepared.source, prepared.loose))
				{
					// A decode that throws fails the whole prepare, as the fused acquire's read
					// failed the whole acquire. Tolerating one would quietly swap the scene's
					// default map in for a texture the caller asked for by name.
					if (!texture.empty() && !out.textures.contains(texture))
						out.textures.emplace(texture, store.LoadTexture(texture));
				}
			}

			return out;
		}
	}

	PreparedMesh
	PrepareMesh(const assetlib::AssetStore& store, std::string_view relPath, uint32_t meshIndex)
	{
		const assetlib::BMesh mesh = store.LoadMesh(relPath);

		PreparedMesh out = PrepareCommon(store, relPath, meshIndex, mesh);
		out.tier         = MeshTier::kStatic;
		return out;
	}

	PreparedMesh
	PrepareVatMesh(
		const assetlib::AssetStore& store,
		std::string_view            relPath,
		std::string_view            animationsRelPath,
		uint32_t                    meshIndex)
	{
		const std::filesystem::path bvatRel = assetlib::vatPathFor(relPath, animationsRelPath);
		const assetlib::BVat        vat     = EnsureVatBaked(store, relPath, animationsRelPath);

		const assetlib::BMesh mesh = store.LoadMesh(relPath);

		PreparedMesh out = PrepareCommon(store, relPath, meshIndex, mesh);
		out.tier         = MeshTier::kVat;
		out.animations   = vat.animations;

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];
		core::throw_runtime_error_if(
			static_cast<size_t>(entry.firstSubmesh) + entry.submeshCount > vat.columns.size(),
			"AssetManager: '{}' does not cover mesh {}'s submeshes",
			bvatRel.string(),
			meshIndex);

		out.vatPositionsKey = bvatRel.string() + "#" + vat.animations + "#positions";
		out.vatNormalsKey   = bvatRel.string() + "#" + vat.animations + "#normals";
		out.vatPositions    = assetlib::decodeKTX2(vat.positionsKtx2);
		out.vatNormals      = assetlib::decodeKTX2(vat.normalsKtx2);

		out.vatDesc.boundsMin = vat.boundsMin;
		out.vatDesc.boundsMax = vat.boundsMax;

		out.vatDesc.clips.reserve(vat.clips.size());
		out.vatClips.reserve(vat.clips.size());
		for (const assetlib::VatClip& clip : vat.clips)
		{
			out.vatDesc.clips.push_back(
				{ clip.firstRow, clip.frameCount, clip.sampleRate, clip.loop != 0 });
			out.vatClips.push_back(
				{ std::string(vat.stringPool.at(clip.nameOffset)),
			      clip.frameCount,
			      clip.sampleRate,
			      clip.duration,
			      clip.loop != 0 });
		}

		out.vatDesc.columnBases.reserve(entry.submeshCount);
		for (uint32_t i = 0; i < entry.submeshCount; ++i)
			out.vatDesc.columnBases.push_back(vat.columns[entry.firstSubmesh + i].columnBase);

		return out;
	}

	PreparedMesh
	PrepareSkinnedMesh(
		const assetlib::AssetStore&            store,
		std::string_view                       relPath,
		std::string_view                       animationsRelPath,
		uint32_t                               meshIndex,
		const std::optional<assetlib::Bounds>& posedBounds)
	{
		const std::string animationsNorm = assetlib::normalizePath(animationsRelPath);

		const assetlib::AnimationSet animations = store.LoadAnimations(animationsNorm);

		// The clip set names its own rig, so the pair cannot be mismatched by a caller -- only by a
		// rig that changed after the clips were cooked, which is what the signature catches.
		const assetlib::Skeleton skeleton = store.LoadSkeleton(animations.skeleton);

		core::throw_runtime_error_if(
			!assetlib::animationsMatchSkeleton(animations, skeleton),
			"AssetManager: '{}' was cooked against a different version of '{}'; a bone has been "
			"inserted, removed or reordered since, so its joint indices name different bones now",
			animationsNorm,
			animations.skeleton);

		const assetlib::BMesh mesh = store.LoadMesh(relPath);

		PreparedMesh out = PrepareCommon(store, relPath, meshIndex, mesh);
		out.tier         = MeshTier::kSkinned;
		out.animations   = animationsNorm;
		out.skeleton     = skeleton;
		out.clips        = animations;

		// The box the geom culls by. Not the bind pose's: a clip carrying root motion walks the rig
		// out of that box, and culling by it makes the mesh vanish as soon as it does.
		out.posedBounds = [&] {
			if (posedBounds)
				return *posedBounds;
			if (const auto baked = assetlib::findPosedBounds(animations, mesh, meshIndex, skeleton))
				return *baked;
			return assetlib::posedBounds(mesh, meshIndex, skeleton, animations);
		}();

		return out;
	}
}
