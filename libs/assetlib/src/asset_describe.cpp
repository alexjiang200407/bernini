#include <assetlib/asset_describe.h>

#include <assetlib/bmaterial_io.h>

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
		modeName(MaterialMode mode)
		{
			return mode == MaterialMode::kLoose ? "loose" : "baked";
		}

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

		// The NUL-terminated name at `offset` in the string pool (empty for offset 0 / out of range).
		std::string
		nameFromPool(const std::vector<char>& pool, uint32_t offset)
		{
			if (offset == 0 || offset >= pool.size())
				return {};
			return std::string(pool.data() + offset);
		}

		std::string
		byteSize(size_t bytes)
		{
			constexpr double kKiB = 1024.0;
			constexpr double kMiB = 1024.0 * 1024.0;

			const auto value = static_cast<double>(bytes);
			if (value >= kMiB)
				return std::format("{:.1f} MiB", value / kMiB);
			if (value >= kKiB)
				return std::format("{:.1f} KiB", value / kKiB);
			return std::format("{} B", bytes);
		}

		std::string
		vec3(const glm::vec3& v)
		{
			return std::format("({:.3g}, {:.3g}, {:.3g})", v.x, v.y, v.z);
		}

		std::string
		texturePathOr(const std::string& path)
		{
			return path.empty() ? std::string("(none)") : path;
		}

		void
		describePbr(std::string& out, const PbrParams& pbr, const std::filesystem::path& dataRoot)
		{
			out += std::format(
				"  baseColorFactor   ({:.3g}, {:.3g}, {:.3g}, {:.3g})\n",
				pbr.baseColorFactor.x,
				pbr.baseColorFactor.y,
				pbr.baseColorFactor.z,
				pbr.baseColorFactor.w);
			out += std::format("  metallicFactor    {:.3g}\n", pbr.metallicFactor);
			out += std::format("  roughnessFactor   {:.3g}\n", pbr.roughnessFactor);

			// The triplet is what a `baked` material draws from; a `loose` one keeps it as the last
			// bake's output, which is why it is printed either way.
			out += "\n  baked textures\n";
			out += std::format("    baseColor       {}\n", texturePathOr(pbr.baseColorTexture));
			out += std::format("    normal          {}\n", texturePathOr(pbr.normalTexture));
			out += std::format("    orm             {}\n", texturePathOr(pbr.ormTexture));

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
				if (dataRoot.empty())
				{
					out += std::format(
						"                    baked from {} B, mtime {}\n",
						baked.size,
						baked.mtime);
					continue;
				}

				const SourceStamp live = stampOf(dataRoot / route.texture);
				if (live == SourceStamp{})
					out += "                    source is missing\n";
				else if (live == baked)
					out += std::format("                    up to date ({} B)\n", live.size);
				else
					out += std::format(
						"                    STALE: source is {} B / mtime {}, baked from {} B / "
						"mtime {}\n",
						live.size,
						live.mtime,
						baked.size,
						baked.mtime);
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

		// Every path is relative to the project's data root, not to this file -- worth saying, since a
		// path that looks broken relative to the .bmesh is usually correct.
		out += std::format(
			"  materials    {} (paths relative to the data root)\n",
			mesh.materials.size());
		for (size_t i = 0; i < mesh.materials.size(); ++i)
			out += std::format("    [{}] {}\n", i, texturePathOr(mesh.materials[i]));

		if (!verbose)
			return out;

		for (size_t i = 0; i < mesh.meshes.size(); ++i)
		{
			const Mesh& entry = mesh.meshes[i];
			out += std::format(
				"\n  mesh [{}] '{}' -- submeshes [{}, {})\n",
				i,
				nameFromPool(mesh.stringPool, entry.nameOffset),
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
					nameFromPool(mesh.stringPool, submesh.nameOffset));
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
	describe(const BMaterial& material, const std::filesystem::path& dataRoot)
	{
		std::string out;

		out += std::format("bmaterial '{}'\n", material.name);
		out += std::format("  shadingModel      {}\n", shadingModelName(material.shadingModel));
		out += std::format("  mode              {}\n", modeName(material.mode));

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			describePbr(out, material.pbr, dataRoot);
			break;

		case ShadingModel::kCount:
			out += "  (unknown shading model; its parameters cannot be described)\n";
			break;
		}

		if (!dataRoot.empty())
			out += std::format(
				"\n  bake              {}\n",
				bakeIsStale(material, dataRoot) ? "STALE" : "up to date");

		out += std::format(
			"  editorGraph       {}\n",
			material.editorGraph.empty() ?
				std::string("(none)") :
				std::format("{} of JSON", byteSize(material.editorGraph.size())));

		return out;
	}
}
