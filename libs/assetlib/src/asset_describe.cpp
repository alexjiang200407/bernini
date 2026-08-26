#include "asset_describe.h"
#include <assetlib/bmesh.h>
#include <assetlib/container_info.h>
#include <assetlib/envmap.h>

#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		// The nine loose channels, in the order BMaterial::routes stores them.
		constexpr std::array<const char*, c_LooseChannelCount> c_ChannelNames = { {
			"baseColor.r",
			"baseColor.g",
			"baseColor.b",
			"baseColor.a",
			"ao",
			"roughness",
			"metallic",
			"normal.x",
			"normal.y",
		} };

		constexpr std::array<const char*, 4> c_ChannelSwizzle = { { "r", "g", "b", "a" } };

		const char*
		shadingModelName(ShadingModel model)
		{
			switch (model)
			{
			case ShadingModel::kPbr:
				return "pbr";
			case ShadingModel::kCount:
				break;
			}
			return "(unknown)";
		}

		const char*
		semanticName(VertexSemantic semantic)
		{
			switch (semantic)
			{
			case VertexSemantic::kPosition:
				return "position";
			case VertexSemantic::kNormal:
				return "normal";
			case VertexSemantic::kTangent:
				return "tangent";
			case VertexSemantic::kColor:
				return "color";
			case VertexSemantic::kTexCoord0:
				return "texcoord0";
			case VertexSemantic::kTexCoord1:
				return "texcoord1";
			case VertexSemantic::kJoints0:
				return "joints0";
			case VertexSemantic::kWeights0:
				return "weights0";
			}
			return "?";
		}

		const char*
		formatName(VertexFormat format)
		{
			switch (format)
			{
			case VertexFormat::kFloat32x2:
				return "float32x2";
			case VertexFormat::kFloat32x3:
				return "float32x3";
			case VertexFormat::kFloat32x4:
				return "float32x4";
			case VertexFormat::kUnorm8x4:
				return "unorm8x4";
			case VertexFormat::kUnorm16x2:
				return "unorm16x2";
			case VertexFormat::kUnorm16x4:
				return "unorm16x4";
			case VertexFormat::kUint16x4:
				return "uint16x4";
			}
			return "?";
		}

		const char*
		indexTypeName(IndexType type)
		{
			switch (type)
			{
			case IndexType::kNone:
				return "none";
			case IndexType::kUint16:
				return "u16";
			case IndexType::kUint32:
				return "u32";
			}
			return "?";
		}

		std::string
		byteSize(size_t bytes)
		{
			constexpr double c_KiB = 1024.0;
			constexpr double c_MiB = 1024.0 * 1024.0;

			const auto value = static_cast<double>(bytes);
			if (value >= c_MiB)
				return std::format("{:.1f} MiB", value / c_MiB);
			if (value >= c_KiB)
				return std::format("{:.1f} KiB", value / c_KiB);
			return std::format("{} B", bytes);
		}

		std::string
		vec3(const glm::vec3& v)
		{
			return std::format("({:.3g}, {:.3g}, {:.3g})", v.x, v.y, v.z);
		}

		std::string
		pathOr(const std::string& path)
		{
			return path.empty() ? std::string("(none)") : path;
		}

		// One authored map: where it came from, what the bake wrote, and whether the two still agree.
		// Shared by the sky and both halves of the lighting, so the three read alike.
		void
		describeEnvRoute(
			std::string&                   out,
			const char*                    label,
			const EnvMapRoute&             route,
			const core::file::IFileSystem* fileSystem)
		{
			out += std::format("\n  {}\n", label);
			out += std::format("    source          {}\n", pathOr(route.source));
			out += std::format("    baked           {}\n", pathOr(route.baked));

			if (route.source.empty())
			{
				out += "    (unrouted)\n";
				return;
			}

			if (fileSystem == nullptr)
			{
				out += std::format(
					"    baked from {} B, hash {:016x}\n",
					route.stamp.size,
					route.stamp.hash);
				return;
			}

			// A map that is named but gone is what makes the route stale even when its source has not
			// moved, so it has to be said here -- otherwise the route reads "up to date" beside a
			// verdict of STALE and the two look like a contradiction.
			if (!route.baked.empty() && stampOf(*fileSystem, route.baked).size == 0)
				out += "    baked map is missing\n";

			const SourceStamp live = stampOf(*fileSystem, route.source);
			if (live == SourceStamp{})
				out += "    source is missing\n";
			else if (live == route.stamp)
				out += std::format("    source up to date ({} B)\n", live.size);
			else
				out += std::format(
					"    STALE: source is {} B / hash {:016x}, baked from {} B / hash {:016x}\n",
					live.size,
					live.hash,
					route.stamp.size,
					route.stamp.hash);
		}

		// A `.benv` names files rather than holding them, so whether the name resolves is the question
		// worth answering. Without a root there is nothing to resolve against.
		std::string
		referenceOr(const std::string& path, const core::file::IFileSystem* fileSystem)
		{
			if (path.empty())
				return "(unset)";
			if (fileSystem == nullptr)
				return path;

			return fileSystem->Exists(path) ? path : std::format("{} (missing)", path);
		}

		std::string_view
		alphaModeName(AlphaMode mode) noexcept
		{
			switch (mode)
			{
			case AlphaMode::kMask:
				return "mask";
			case AlphaMode::kBlend:
				return "blend";
			case AlphaMode::kHashed:
				return "hashed";
			case AlphaMode::kOpaque:
				break;
			}
			return "opaque";
		}

		void
		describePbr(
			std::string&                   out,
			const PbrParams&               pbr,
			const core::file::IFileSystem* fileSystem)
		{
			out += std::format(
				"  baseColorFactor   ({:.3g}, {:.3g}, {:.3g}, {:.3g})\n",
				pbr.baseColorFactor.x,
				pbr.baseColorFactor.y,
				pbr.baseColorFactor.z,
				pbr.baseColorFactor.w);
			out += std::format("  metallicFactor    {:.3g}\n", pbr.metallicFactor);
			out += std::format("  roughnessFactor   {:.3g}\n", pbr.roughnessFactor);
			out += std::format("  transmission      {:.3g}\n", pbr.transmissionFactor);
			out += std::format(
				"  specular          {:.3g} x ({:.3g}, {:.3g}, {:.3g})\n",
				pbr.specularFactor,
				pbr.specularColorFactor.x,
				pbr.specularColorFactor.y,
				pbr.specularColorFactor.z);

			// The animated tiers draw opaque geometry only, so this is the field that decides whether
			// a submesh can be skinned at all.
			out += std::format("  alphaMode         {}\n", alphaModeName(pbr.alphaMode));

			// The triplet is what a `baked` material draws from; a `loose` one keeps it as the last
			// bake's output, which is why it is printed either way.
			out += "\n  baked textures\n";
			out += std::format("    baseColor       {}\n", pathOr(pbr.baseColorTexture));
			out += std::format("    normal          {}\n", pathOr(pbr.normalTexture));
			out += std::format("    orm             {}\n", pathOr(pbr.ormTexture));

			out += "\n  channel routes\n";
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
			{
				const ChannelRoute& route = pbr.routes[i];
				if (route.texture.empty())
				{
					out += std::format("    {:<15} (unrouted)\n", c_ChannelNames[i]);
					continue;
				}

				const char* swizzle =
					route.channel < c_ChannelSwizzle.size() ? c_ChannelSwizzle[route.channel] : "?";

				out +=
					std::format("    {:<15} {} [{}]\n", c_ChannelNames[i], route.texture, swizzle);

				// Compare the source as it is now against the stamp taken when the bake ran. Without a
				// data root there is nothing to stat, so only the recorded stamp is reported.
				const SourceStamp& baked = pbr.routeStamps[i];
				if (fileSystem == nullptr)
				{
					out += std::format(
						"                    baked from {} B, hash {:016x}\n",
						baked.size,
						baked.hash);
					continue;
				}

				const SourceStamp live = stampOf(*fileSystem, route.texture);
				if (live == SourceStamp{})
					out += "                    source is missing\n";
				else if (live == baked)
					out += std::format("                    up to date ({} B)\n", live.size);
				else
					out += std::format(
						"                    STALE: source is {} B / hash {:016x}, baked from {} B "
						"/ "
						"hash {:016x}\n",
						live.size,
						live.hash,
						baked.size,
						baked.hash);
			}
		}

		void
		describeLayout(std::string& out, const VertexLayout& layout)
		{
			out += std::format("      layout   stride {} B:", layout.stride);
			for (uint8_t i = 0; i < layout.attributeCount; ++i)
			{
				const VertexAttribute& attribute = layout.attributes[i];
				out += std::format(
					" {}:{}@{}",
					semanticName(attribute.semantic),
					formatName(attribute.format),
					attribute.offset);
			}
			out += '\n';
		}
	}

	std::string
	describe(const BMesh& mesh, bool verbose)
	{
		std::string out;

		out += "bmesh\n";
		out +=
			std::format("  nodes        {} ({} root(s))\n", mesh.nodes.size(), mesh.roots.size());
		out += std::format("  meshes       {}\n", mesh.meshes.size());
		out += std::format("  submeshes    {}\n", mesh.submeshes.size());
		out += std::format("  meshlets     {}\n", mesh.meshlets.size());
		out += std::format("  vertexData   {}\n", byteSize(mesh.vertexData.size()));
		out += std::format("  indexData    {}\n", byteSize(mesh.indexData.size()));

		// A mesh whose layout carries joints but names no skeleton has joint indices nothing can
		// resolve, which is invisible until it renders as a heap.
		if (isSkinned(mesh))
		{
			out += std::format(
				"  skeleton     {}\n",
				mesh.skeleton.empty() ? "(SKINNED, but names none)" : mesh.skeleton);
			out += std::format("  signature    {:016x}\n", mesh.skeletonSignature);
		}
		else if (!mesh.skeleton.empty())
			out += std::format(
				"  skeleton     {} (unused: no submesh carries joints)\n",
				mesh.skeleton);

		// Every path is relative to the project's data root, not to this file -- worth saying, since a
		// path that looks broken relative to the .bmesh is usually correct.
		out += std::format(
			"  materials    {} (paths relative to the data root)\n",
			mesh.materials.size());
		for (size_t i = 0; i < mesh.materials.size(); ++i)
			out += std::format("    [{}] {}\n", i, pathOr(mesh.materials[i]));

		if (!verbose)
			return out;

		for (size_t i = 0; i < mesh.meshes.size(); ++i)
		{
			const Mesh& entry = mesh.meshes[i];
			out += std::format(
				"\n  mesh [{}] '{}' -- submeshes [{}, {})\n",
				i,
				mesh.stringPool.at(entry.nameOffset),
				entry.firstSubmesh,
				entry.firstSubmesh + entry.submeshCount);

			for (uint32_t s = 0; s < entry.submeshCount; ++s)
			{
				const uint32_t index   = entry.firstSubmesh + s;
				const Submesh& submesh = mesh.submeshes[index];

				// A submesh whose material index is out of range draws with the renderer's default
				// material, so call it out rather than printing a bare number.
				const std::string material =
					submesh.material < mesh.materials.size() ?
						std::format("[{}] {}", submesh.material, mesh.materials[submesh.material]) :
						std::format("[{}] (out of range -- no material)", submesh.material);

				out += std::format(
					"    submesh [{}] '{}'\n",
					index,
					mesh.stringPool.at(submesh.nameOffset));
				out += std::format(
					"      geometry {} verts, {} indices ({}), {} meshlets\n",
					submesh.vertexCount,
					submesh.indexCount,
					indexTypeName(submesh.indexType),
					submesh.meshletCount);
				out += std::format("      material {}\n", material);
				describeLayout(out, submesh.layout);
				out += std::format(
					"      aabb     {} .. {}\n",
					vec3(submesh.aabbMin),
					vec3(submesh.aabbMax));
			}
		}

		return out;
	}

	std::string
	describe(const BMaterial& material, const core::file::IFileSystem* fileSystem)
	{
		std::string out;

		out += std::format("bmaterial '{}'\n", material.name);
		out += std::format("  shadingModel      {}\n", shadingModelName(material.shadingModel));

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			describePbr(out, material.pbr, fileSystem);
			break;

		case ShadingModel::kCount:
			out += "  (unknown shading model; its parameters cannot be described)\n";
			break;
		}

		if (fileSystem != nullptr)
		{
			out += std::format(
				"\n  bake              {}\n  draws from        {}\n",
				bakeIsStale(material, *fileSystem) ? "STALE" : "up to date",
				drawsLoose(material, *fileSystem) ? "routes (loose)" : "baked triplet");
		}

		out += std::format(
			"  editorGraph       {}\n",
			material.editorGraph.empty() ?
				std::string("(none)") :
				std::format("{} of JSON", byteSize(material.editorGraph.size())));

		return out;
	}

	std::string
	describe(const BSky& sky, const core::file::IFileSystem* fileSystem)
	{
		std::string out;

		out += std::format("bsky '{}'\n", sky.name);

		describeEnvRoute(out, "sky", sky.sky, fileSystem);

		if (fileSystem != nullptr)
			out += std::format(
				"\n  bake              {}\n",
				isSkyBakeStale(sky, *fileSystem) ? "STALE" : "up to date");

		return out;
	}

	std::string
	describe(const BEnvLighting& lighting, const core::file::IFileSystem* fileSystem)
	{
		std::string out;

		out += std::format("benvl '{}'\n", lighting.name);
		out += std::format("  exposure          {:.3g}\n", lighting.exposure);

		describeEnvRoute(out, "prefilter", lighting.prefilter, fileSystem);
		describeEnvRoute(out, "irradiance", lighting.irradiance, fileSystem);

		// One verdict, not one per route: the two are convolutions of the same radiance, so either
		// having drifted makes the pair untrustworthy.
		if (fileSystem != nullptr)
			out += std::format(
				"\n  bake              {}\n",
				isEnvLightingBakeStale(lighting, *fileSystem) ? "STALE" : "up to date");

		return out;
	}

	std::string
	describe(const BEnv& env, const core::file::IFileSystem* fileSystem)
	{
		std::string out;

		out += std::format("benv '{}'\n", env.name);
		out += std::format("  shadingModel      {}\n", shadingModelName(env.shadingModel));
		out += std::format("  sky               {}\n", referenceOr(env.sky, fileSystem));
		out +=
			std::format("  skyMipLevel       {} (requested; resolution clamps)\n", env.skyMipLevel);
		out += std::format(
			"  skyRotationY      {:.3g} rad ({:.3g} deg)\n",
			env.skyRotationY,
			glm::degrees(env.skyRotationY));
		out += std::format(
			"  rim               tint ({:.3g}, {:.3g}, {:.3g}) intensity {:.3g} power {:.3g}\n",
			env.rim.tint.r,
			env.rim.tint.g,
			env.rim.tint.b,
			env.rim.intensity,
			env.rim.power);

		switch (env.shadingModel)
		{
		case ShadingModel::kPbr:
			out +=
				std::format("  lighting          {}\n", referenceOr(env.pbr.lighting, fileSystem));
			out += std::format(
				"  exposureOverride  {}\n",
				env.pbr.exposureOverride.has_value() ?
					std::format("{:.6g}", *env.pbr.exposureOverride) :
					std::string("(unset; the lighting's derivation stands)"));
			break;
		case ShadingModel::kCount:
			break;
		}

		return out;
	}

	std::string
	describe(const Skeleton& skeleton)
	{
		std::string out;

		out += "bskel\n";
		out += std::format("  bones        {}\n", skeleton.bones.size());
		out += std::format("  signature    {:016x}\n", skeletonSignature(skeleton));

		for (size_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const Bone& bone = skeleton.bones[i];
			out += std::format(
				"    [{}] '{}' parent {} bind t{} s{}\n",
				i,
				skeleton.stringPool.at(bone.nameOffset),
				bone.parent == c_InvalidIndex ? std::string("(root)") : std::to_string(bone.parent),
				vec3(bone.bindPose.translation),
				vec3(bone.bindPose.scale));
		}

		return out;
	}

	std::string
	describe(const AnimationSet& animations, const Skeleton* skeleton)
	{
		std::string out;

		out += "banim\n";
		out += std::format(
			"  skeleton     {} (path relative to the data root)\n",
			pathOr(animations.skeleton));
		out += std::format("  bones        {}\n", animations.boneCount);
		out += std::format("  signature    {:016x}\n", animations.skeletonSignature);

		if (skeleton != nullptr)
			out += std::format(
				"  binding      {}\n",
				animationsMatchSkeleton(animations, *skeleton) ?
					"matches the skeleton" :
					"DOES NOT MATCH the skeleton -- the clips' joint indices name other bones");

		out += std::format("  clips        {}\n", animations.clips.size());
		out += std::format(
			"  samples      {} ({})\n",
			animations.samples.size(),
			byteSize(animations.samples.size() * sizeof(Transform)));

		if (animations.posedBoxes.empty())
			out += "  posedBoxes   none (a load measures instead -- see assetlib_cli bakebounds)\n";
		for (const PosedBox& box : animations.posedBoxes)
			out += std::format(
				"  posedBox     mesh {} of source {:016x}: {} .. {}\n",
				box.meshIndex,
				box.sourceSignature,
				vec3(box.min),
				vec3(box.max));

		for (size_t i = 0; i < animations.clips.size(); ++i)
		{
			const AnimationClip& clip = animations.clips[i];
			out +=
				std::format("\n  clip [{}] '{}'\n", i, animations.stringPool.at(clip.nameOffset));
			out += std::format(
				"    length     {:.3g} s, {} frames at {:.4g} Hz{}\n",
				clip.duration,
				clip.frameCount,
				clip.sampleRate,
				clip.loop != 0 ? ", looping" : "");
			out += std::format(
				"    rootMotion {} ({:.4g} u/s)\n",
				vec3(clip.rootMotion),
				clip.locomotionSpeed);
			out +=
				std::format("    ground     moved down {:.4g} to rest on y 0\n", clip.groundOffset);
		}

		return out;
	}

	namespace
	{
		// One input of a VAT bake: the path, the stamp the bake recorded, and -- against a data
		// root -- whether the file still matches it.
		void
		describeVatInput(
			std::string&                   out,
			const char*                    label,
			const std::string&             path,
			const SourceStamp&             baked,
			const core::file::IFileSystem* fileSystem)
		{
			out += std::format("    {:<10} {}\n", label, pathOr(path));

			if (fileSystem == nullptr || path.empty())
				return;

			const SourceStamp live = stampOf(*fileSystem, path);
			if (live.size == 0)
				out += "               MISSING: the file is not on disk\n";
			else if (live != baked)
				out += std::format(
					"               STALE: source is {} B / hash {:016x}, baked from {} B / hash "
					"{:016x}\n",
					live.size,
					live.hash,
					baked.size,
					baked.hash);
		}
	}

	std::string
	describe(const BVat& vat, const core::file::IFileSystem* fileSystem)
	{
		std::string out;

		out += "bvat\n";
		out += std::format(
			"  textures     {} x {} (vertex columns x padded frame rows)\n",
			vat.width,
			vat.height);
		// Empty payloads mean a tables-only read, not an empty texture -- serializeVat refuses those.
		const auto payload = [](const std::vector<std::byte>& bytes) {
			return bytes.empty() ? std::string("(not read)") : byteSize(bytes.size());
		};
		out += std::format(
			"  positions    {} (RGBA16 unorm in bounds), normals {} (RGBA8 unorm)\n",
			payload(vat.positionsKtx2),
			payload(vat.normalsKtx2));
		out += std::format(
			"  bounds       {} .. {} (all clips)\n",
			vec3(vat.boundsMin),
			vec3(vat.boundsMax));
		out += std::format(
			"  palettes     {} ({} bones x {} frames)\n",
			vat.palettes.size(),
			vat.boneCount,
			vat.boneCount != 0 ? vat.palettes.size() / vat.boneCount : 0);

		out += std::format("  inputs       (paths relative to the data root)\n");
		describeVatInput(out, "mesh", vat.mesh, vat.meshStamp, fileSystem);
		describeVatInput(out, "skeleton", vat.skeleton, vat.skeletonStamp, fileSystem);
		describeVatInput(out, "animations", vat.animations, vat.animationsStamp, fileSystem);
		out += std::format("    signature  {:016x}\n", vat.skeletonSignature);

		out += std::format("  submeshes    {}\n", vat.columns.size());
		for (size_t i = 0; i < vat.columns.size(); ++i)
			out += std::format(
				"    [{}] columns [{}, {})\n",
				i,
				vat.columns[i].columnBase,
				vat.columns[i].columnBase + vat.columns[i].vertexCount);

		out += std::format("  clips        {}\n", vat.clips.size());
		for (size_t i = 0; i < vat.clips.size(); ++i)
		{
			const VatClip& clip = vat.clips[i];
			out += std::format("\n  clip [{}] '{}'\n", i, vat.stringPool.at(clip.nameOffset));
			out += std::format(
				"    length     {:.3g} s, {} frames at {:.4g} Hz{}\n",
				clip.duration,
				clip.frameCount,
				clip.sampleRate,
				clip.loop != 0 ? ", looping" : "");
			out += std::format(
				"    rows       [{}, {}] + padding row {}\n",
				clip.firstRow,
				clip.firstRow + clip.frameCount - 1,
				clip.firstRow + clip.frameCount);
		}

		return out;
	}

	// The public form: what a container records, with nothing stat'd. AssetStore::Describe is the
	// one that also checks whether what it records is still true.
	std::string
	describe(const BMaterial& material)
	{
		return describe(material, nullptr);
	}

	std::string
	describe(const BSky& sky)
	{
		return describe(sky, nullptr);
	}

	std::string
	describe(const BEnvLighting& lighting)
	{
		return describe(lighting, nullptr);
	}

	std::string
	describe(const BEnv& env)
	{
		return describe(env, nullptr);
	}

	std::string
	describe(const BVat& vat)
	{
		return describe(vat, nullptr);
	}
}
