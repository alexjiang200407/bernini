#include <algorithm>
#include <array>
#include <assetlib/bmaterial.h>
#include <assetlib/container_info.h>
#include <assetlib/material_bake.h>

#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>

#include "baked_name.h"
#include "bmesh_texture.h"
#include "fs_util.h"
#include <assetlib_structs/VkFormat.h>

#include <core/err/util.h>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stb_image_resize2.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace assetlib
{
	namespace
	{
		// What a bake reads and writes: the store's data root, and the directory baked maps land in
		// relative to it. Not public -- a caller names the store, which already holds the root.
		struct BakeDesc
		{
			std::filesystem::path dataRoot;
			std::filesystem::path textureDir;

			// False resolves the triplet and encodes nothing, which is what naming a bake's output
			// without paying for one takes.
			bool write = true;
		};
	}

	namespace
	{
		// One decoded source texture: tightly packed RGBA8 at its own resolution.
		using Rgba8 = std::vector<std::byte>;

		struct Source
		{
			Rgba8    pixels;
			uint32_t width  = 0;
			uint32_t height = 0;
			bool     srgb   = false;
		};

		// The three maps a PBR material bakes to.
		struct Group
		{
			ChannelGroup    channels;
			const char*     name;      // file-name prefix, also part of the content hash
			uint8_t         fallback;  // what an unrouted channel of this group samples
			bool            srgb;
			Ktx2Compression compression;
		};

		constexpr std::array<Group, 3> c_Groups = { {
			{ c_BaseColorChannels, "basecolor", 0xFF, true, Ktx2Compression::kBC1_RGB },
			{ c_OrmChannels, "orm", 0xFF, false, Ktx2Compression::kBC7_RGBA },
			// An unrouted normal axis is 0.5, i.e. zero once the shader maps [0,1] to [-1,1].
			{ c_NormalChannels, "normal", 0x80, false, Ktx2Compression::kBC5_RG },
		} };

		// Every routed surface slot bakes under this one prefix, whatever the slot is called: the
		// slot's own name goes in the content key instead, so texture pruning recognises the family
		// without enumerating names no engine list holds.
		constexpr std::string_view c_SurfaceSlotBakePrefix = "slot";

		// The one rule for a slot's map, shared by the disk bake and the in-memory compose: linear
		// data, white where nothing routes (the factor alone drives that component), BC7 on disk.
		constexpr uint8_t         c_SlotFallback    = 0xFF;
		constexpr Ktx2Compression c_SlotCompression = Ktx2Compression::kBC7_RGBA;

		bool
		isRgba8(VkFormat vk)
		{
			return vk == VkFormat::R8G8B8A8_UNORM || vk == VkFormat::R8G8B8A8_SRGB;
		}

		// Mip 0 of `image` as tightly packed RGBA8, dropping any row padding the container had.
		Rgba8
		topMipRgba8(const ImageData& image, const std::string& name)
		{
			// loadKTX2(kRgba8) decodes Basis sources and rejects block-compressed ones, so anything
			// that reaches here should already be RGBA8. Anything else is a format we cannot composite.
			if (!isRgba8(image.vkFormat))
				throw std::runtime_error(
					"assetlib::bakeMaterial: source '" + name +
					"' decoded to an unexpected format (Vulkan format " +
					std::to_string(static_cast<uint32_t>(image.vkFormat)) + ")");
			if (image.subresources.empty())
				throw std::runtime_error(
					"assetlib::bakeMaterial: source '" + name + "' has no image data");

			const ImageSubresource& sub    = image.subresources.front();
			const size_t            stride = static_cast<size_t>(image.width) * 4u;

			Rgba8 out(stride * image.height);
			for (uint32_t y = 0; y < image.height; ++y)
			{
				std::memcpy(
					out.data() + static_cast<size_t>(y) * stride,
					image.pixels.data() + sub.offset + static_cast<size_t>(y) * sub.rowPitch,
					stride);
			}
			return out;
		}

		// `pixels` (srcW x srcH RGBA8) resampled to dstW x dstH, in linear light when the source is
		// sRGB. A no-op when the extents already match.
		Rgba8
		resample(
			const Rgba8& pixels,
			bool         srgb,
			uint32_t     srcW,
			uint32_t     srcH,
			uint32_t     dstW,
			uint32_t     dstH)
		{
			if (srcW == dstW && srcH == dstH)
				return pixels;

			const auto resize = srgb ? stbir_resize_uint8_srgb : stbir_resize_uint8_linear;

			Rgba8 out(static_cast<size_t>(dstW) * dstH * 4u);
			if (resize(
					reinterpret_cast<const unsigned char*>(pixels.data()),
					static_cast<int>(srcW),
					static_cast<int>(srcH),
					0,
					reinterpret_cast<unsigned char*>(out.data()),
					static_cast<int>(dstW),
					static_cast<int>(dstH),
					0,
					STBIR_RGBA) == nullptr)
			{
				throw std::runtime_error("assetlib::bakeMaterial: source resize failed");
			}
			return out;
		}

		/**
		 * The material's routed sources, each decoded at most once and only when something asks for it.
		 *
		 * Decoding is the expensive half of a bake and a map already on disk needs none of it, so a
		 * material whose every group is current transcodes nothing.
		 */
		class SourceCache
		{
		public:
			explicit SourceCache(std::filesystem::path dataRoot) noexcept :
				m_DataRoot(std::move(dataRoot))
			{}

			const Source&
			Get(const std::string& texture)
			{
				if (const auto decoded = m_Decoded.find(texture); decoded != m_Decoded.end())
					return decoded->second;

				// Sources are read, not drawn: decode Basis to texels rather than to BC7 blocks.
				const ImageData image = loadKTX2(m_DataRoot / texture, Ktx2Decode::kRgba8);

				Source source;
				source.pixels = topMipRgba8(image, texture);
				source.width  = image.width;
				source.height = image.height;
				source.srgb   = image.vkFormat == VkFormat::R8G8B8A8_SRGB;
				return m_Decoded.emplace(texture, std::move(source)).first->second;
			}

		private:
			std::filesystem::path                m_DataRoot;
			core::str::unordered_str_map<Source> m_Decoded;
		};

		/**
		 * The size + content hash of every distinct source `routes` name, merged into `stamps`.
		 *
		 * Read up front because both halves of the bake need it: the key each map is named by, and the
		 * `routeStamps` written back once the maps are current.
		 *
		 * @throws std::runtime_error if a routed source cannot be read. Routing nothing is not an
		 *         error: see bakeMaterial.
		 */
		void
		stampRoutes(
			std::span<const ChannelRoute>              routes,
			const std::filesystem::path&               dataRoot,
			core::str::unordered_str_map<SourceStamp>& stamps)
		{
			for (const ChannelRoute& route : routes)
			{
				if (route.texture.empty() || stamps.contains(route.texture))
					continue;

				const SourceStamp stamp = stampOf(dataRoot / route.texture);
				if (stamp.size == 0)
					throw std::runtime_error(
						"assetlib::bakeMaterial: source '" + route.texture + "' cannot be read");

				stamps.emplace(route.texture, stamp);
			}
		}

		bool
		anyRouted(std::span<const ChannelRoute> routes) noexcept
		{
			return std::ranges::any_of(routes, [](const ChannelRoute& route) {
				return !route.texture.empty();
			});
		}

		/** The `group.count`-long slice of the PBR routes (or stamps) one map composites. */
		template <typename T>
		std::span<T>
		groupSlice(std::span<T> all, const ChannelGroup& group) noexcept
		{
			return all.subspan(channelIndex(group, 0), group.count);
		}

		/**
		 * Whether this group has to carry a real alpha channel.
		 *
		 * Base color is the only 4-channel group, so it is the only one with an alpha component at all
		 * -- ORM and normal have none. Whether that component matters is the material's authored alpha
		 * mode, *not* something inferred from the routes: an importer that wires all four channels of
		 * every texture out of habit would otherwise turn every material into a cutout. Every alpha
		 * mode that reads the channel -- test (kMask), blend (kBlend) and stochastic coverage
		 * (kHashed) -- keeps it, so it bakes BC7.
		 */
		bool
		groupCarriesAlpha(const MaterialLayer& layer, const Group& group)
		{
			return group.channels.count == c_BaseColorChannels.count &&
			       (layer.alphaMode == AlphaMode::kMask || layer.alphaMode == AlphaMode::kBlend ||
			        layer.alphaMode == AlphaMode::kHashed);
		}

		/**
		 * Whether this group's mips are coverage-preserving, which only a *threshold* on alpha needs.
		 *
		 * The alpha test compares alpha against a constant, so averaging a thin mask down pulls it
		 * under the cutoff and the geometry dissolves; rescaling each level against the cutoff is what
		 * stops that. Hashed compares alpha against a uniform random threshold instead: a fragment
		 * survives with probability equal to its alpha, so the mean alpha a box filter produces *is*
		 * the expected coverage. Rescaling there does not preserve coverage, it manufactures it -- and
		 * on a grating, whose levels average to near-uniform alpha with no scale landing between
		 * "nothing passes" and "everything does", it drives the whole level over the cutoff and the
		 * strands bake into a solid block. Hair is a grating.
		 *
		 * What keeps a distant hashed strand from fading is `SharpenMinifiedAlpha`, which steepens
		 * about the level's own mean and so needs that mean to be true. Blend keeps the channel and
		 * takes plain mips for the same reason: dilution is the prefiltering blending wants.
		 */
		bool
		groupPreservesCoverage(const MaterialLayer& layer, const Group& group)
		{
			return group.channels.count == c_BaseColorChannels.count &&
			       layer.alphaMode == AlphaMode::kMask;
		}

		/**
		 * The block format this group bakes to, which is a property of the *material*, not of the group
		 * alone.
		 *
		 */
		Ktx2Compression
		groupCompression(const MaterialLayer& layer, const Group& group)
		{
			return groupCarriesAlpha(layer, group) ? Ktx2Compression::kBC7_RGBA : group.compression;
		}

		// A map is sized to the largest source routed into *it*, so its output does not depend on any
		// texture outside its routes -- which is what lets two materials share the baked file.
		std::pair<uint32_t, uint32_t>
		mapExtent(std::span<const ChannelRoute> routes, SourceCache& sources)
		{
			uint32_t width  = 0;
			uint32_t height = 0;
			for (const ChannelRoute& route : routes)
			{
				if (route.texture.empty())
					continue;

				const Source& source = sources.Get(route.texture);
				width                = (std::max)(width, source.width);
				height               = (std::max)(height, source.height);
			}
			return { width, height };
		}

		/**
		 * Everything that determines a baked map's bytes, as a canonical string: the caller's lead
		 * (name, token and *resolved* format, from keyLead, plus whatever else feeds the bytes --
		 * the base-colour cut segment, a surface slot's name) and the ordered (source, channel,
		 * source content) triple feeding each component. Two materials that agree on all of this
		 * produce byte-identical output, so they should -- and do -- name the same file.
		 *
		 * The target resolution is deliberately absent: a map is sized to the largest source routed
		 * into it, so identical source content already implies it -- and leaving it out is what lets a
		 * bake decide without decoding an image. See docs/asset_standards.md.
		 *
		 * c_TextureBakeToken leads, so a revision of the chain itself takes a new name rather than
		 * finding the old one already on disk.
		 */
		std::string
		bakeKey(
			std::string                                      lead,
			std::span<const ChannelRoute>                    routes,
			const core::str::unordered_str_map<SourceStamp>& stamps,
			uint8_t                                          fallback)
		{
			std::string key = std::move(lead);
			for (const ChannelRoute& route : routes)
			{
				key += '|';

				if (route.texture.empty())
				{
					key += ':' + std::to_string(fallback);
					continue;
				}

				const SourceStamp& stamp = stamps.at(route.texture);
				key += route.texture;
				key += ':' + std::to_string(route.channel);
				key += '@' + std::to_string(stamp.size) + ':' + std::to_string(stamp.hash);
			}
			return key;
		}

		/** The lead every map's key starts from -- everything before the per-route segments. */
		std::string
		keyLead(std::string_view name, Ktx2Compression compression)
		{
			return std::string(name) + '|' + std::to_string(c_TextureBakeToken) + '|' +
			       std::to_string(static_cast<uint32_t>(compression));
		}

		/**
		 * Gathers a map's channels into packed RGBA8 at `width` x `height`. Destination component i
		 * takes its routed source's channel; an unrouted one takes `fallback`, the value that makes
		 * the shader's factor alone drive that output. Components past the routes' count stay 255
		 * (BC1 ignores alpha; BC5 ignores B and A).
		 */
		Rgba8
		compose(
			std::span<const ChannelRoute> routes,
			uint8_t                       fallback,
			SourceCache&                  sources,
			uint32_t                      width,
			uint32_t                      height)
		{
			const size_t texels = static_cast<size_t>(width) * height;

			// Resampling is per (source, extent), so a source feeding two components of one group is
			// only scaled once.
			auto scaled = core::str::unordered_str_map<Rgba8>();

			Rgba8 out(texels * 4u, std::byte{ 0xFF });
			for (size_t component = 0; component < routes.size(); ++component)
			{
				const ChannelRoute& route = routes[component];

				if (route.texture.empty())
				{
					for (size_t t = 0; t < texels; ++t)
						out[t * 4u + component] = static_cast<std::byte>(fallback);
					continue;
				}

				if (!scaled.contains(route.texture))
				{
					const Source& source = sources.Get(route.texture);
					scaled.emplace(
						route.texture,
						resample(
							source.pixels,
							source.srgb,
							source.width,
							source.height,
							width,
							height));
				}

				const Rgba8& src     = scaled.at(route.texture);
				const size_t channel = (std::min)(static_cast<size_t>(route.channel), size_t{ 3 });
				for (size_t t = 0; t < texels; ++t) out[t * 4u + component] = src[t * 4u + channel];
			}
			return out;
		}

		// Whether a map is already on disk under the name bakeKey resolved to, which is the whole
		// up-to-date test: that name covers everything determining the bytes, sources included.
		//
		// A stat, not a stamp: this only has to find the file, and hashing a 4K map to learn it exists
		// costs more than the question is worth.
		bool
		hasBytes(const std::filesystem::path& target)
		{
			std::error_code ec;
			const auto      size = std::filesystem::file_size(target, ec);
			return !ec && size > 0;
		}
	}

	static void
	bakePbr(BMaterial& material, const BakeDesc& desc, const CancelToken& cancel)
	{
		const MaterialLayer& layer = material.layer;
		PbrParams&           pbr   = material.pbr;

		auto stamps = core::str::unordered_str_map<SourceStamp>();
		stampRoutes(pbr.routes, desc.dataRoot, stamps);

		// Routing nothing is a complete material, not a failed one: its factors are the whole
		// description, and the triplet it may already carry is what draws it. Nothing to composite is
		// nothing to do -- the same verdict bakeIsStale reaches -- so it must not fail a batch that a
		// hundred other materials are in.
		if (stamps.empty())
			return;

		SourceCache sources(desc.dataRoot);

		const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
		if (desc.write)
			createDirectories(outDir);

		// Which triplet field each group fills, in c_Groups order.
		std::string* const outputs[] = {
			&pbr.baseColorTexture,
			&pbr.ormTexture,
			&pbr.normalTexture,
		};

		std::array<std::string, c_Groups.size()> baked;

		for (size_t g = 0; g < c_Groups.size(); ++g)
		{
			const Group& group = c_Groups[g];
			const auto   routes =
				groupSlice(std::span<const ChannelRoute>(pbr.routes), group.channels);

			// A group with nothing routed is not baked at all: an empty triplet entry makes the runtime
			// fall back to white / flat-normal, exactly what an all-default map would have been.
			if (!anyRouted(routes))
				continue;

			throwIfCancelled(cancel);

			const Ktx2Compression compression = groupCompression(layer, group);

			// Cutout and hashed mips are keyed against the cutoff; blend keeps its alpha but bakes
			// plain mips.
			const std::optional<float> mipCutoff = groupPreservesCoverage(layer, group) ?
			                                           std::optional(layer.alphaCutoff) :
			                                           std::nullopt;

			// The cut segment is written for base colour whether or not there is a cutoff, so a
			// cutout and a blend material never converge on one file name.
			std::string lead = keyLead(group.name, compression);
			if (group.channels.count == c_BaseColorChannels.count)
				lead += mipCutoff ? "|cut:" + std::to_string(*mipCutoff) : "|cut:none";

			const std::string name = bakedMapFileName(
				group.name,
				bakeKey(std::move(lead), routes, stamps, group.fallback));
			const auto target = outDir / name;

			if (desc.write && !hasBytes(target))
			{
				const auto [width, height] = mapExtent(routes, sources);

				Rgba8 composed = compose(routes, group.fallback, sources, width, height);

				// A cutout/blend base colour keeps its alpha channel, so bleed opaque colour under the
				// transparent texels before BC7 sees them -- otherwise a block on a cutout edge stores
				// arbitrary colour there and it fringes back across the edge (worst at coarse mips).
				if (groupCarriesAlpha(layer, group))
				{
					dilateColorIntoTransparent(composed, width, height);
				}

				const ImageData image =
					rgba8ToImage(composed, width, height, mipCutoff, group.srgb);

				writeKTX2(image, target, group.srgb, compression);
			}

			// Recorded relative to the data root, not to the material file.
			baked[g] = (desc.textureDir / name).generic_string();
		}

		for (size_t g = 0; g < c_Groups.size(); ++g)
			if (!baked[g].empty())
				*outputs[g] = baked[g];

		// Record what each source measured, so a later edit to one of them shows up as a stale bake.
		for (size_t i = 0; i < c_LooseChannelCount; ++i)
		{
			const std::string& texture = pbr.routes[i].texture;
			pbr.routeStamps[i]         = texture.empty() ? SourceStamp{} : stamps.at(texture);
		}
		pbr.bakeToken = c_TextureBakeToken;

		// The routes stay: they are how it gets re-baked, and what it draws from until then.
	}

	// A routed surface slot bakes as ADR-7 in the surface-material-panel plan records: linear
	// data, white fallback, BC7, under one shared "slot" prefix so texture pruning can recognise
	// the family without enumerating slot names. The slot's declared name is in the key, so two
	// slots routing the same sources still name distinct files only when their names differ --
	// identical content under one name is the same sharing the PBR groups have.
	static void
	bakeSurface(BMaterial& material, const BakeDesc& desc, const CancelToken& cancel)
	{
		auto stamps = core::str::unordered_str_map<SourceStamp>();
		for (const SurfaceTextureBinding& slot : material.surface.textures)
			stampRoutes(slot.routes, desc.dataRoot, stamps);

		// Binding everything whole is the common surface material, and it has nothing to bake --
		// the loop below then only zeroes what is already zero, and touches no directory.
		SourceCache sources(desc.dataRoot);

		const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
		if (desc.write && !stamps.empty())
			createDirectories(outDir);

		for (SurfaceTextureBinding& slot : material.surface.textures)
		{
			const auto routes = std::span<const ChannelRoute>(slot.routes);

			// Stamps are zeroed where nothing routes, whether or not the slot bakes -- a de-routed
			// channel must not keep claiming the provenance of a bake it is no longer part of.
			for (size_t i = 0; i < c_SurfaceSlotChannelCount; ++i)
			{
				const std::string& texture = slot.routes[i].texture;
				slot.routeStamps[i]        = texture.empty() ? SourceStamp{} : stamps.at(texture);
			}

			if (!anyRouted(routes))
			{
				// A slot that stopped routing loses its map with it. The PBR triplet stays after a
				// de-route because the material still draws it; an unrouted slot samples its whole
				// binding instead, so a kept map would be dead yet marked live by texture pruning.
				slot.bakedPath.clear();
				slot.bakeToken = 0;
				continue;
			}

			throwIfCancelled(cancel);

			const std::string name = bakedMapFileName(
				c_SurfaceSlotBakePrefix,
				bakeKey(
					keyLead(c_SurfaceSlotBakePrefix, c_SlotCompression) + '|' + slot.name,
					routes,
					stamps,
					c_SlotFallback));
			const auto target = outDir / name;

			if (desc.write && !hasBytes(target))
			{
				const auto [width, height] = mapExtent(routes, sources);

				const Rgba8     composed = compose(routes, c_SlotFallback, sources, width, height);
				const ImageData image = rgba8ToImage(composed, width, height, std::nullopt, false);

				writeKTX2(image, target, false, c_SlotCompression);
			}

			slot.bakedPath = (desc.textureDir / name).generic_string();
			slot.bakeToken = c_TextureBakeToken;
		}
	}

	static void
	bakeMaterial(BMaterial& material, const BakeDesc& desc, const CancelToken& cancel)
	{
		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			bakePbr(material, desc, cancel);
			return;
		case ShadingModel::kPbrSurface:
			bakeSurface(material, desc, cancel);
			return;
		case ShadingModel::kCount:
			break;
		}
		core::throw_runtime_error(
			"assetlib::bakeMaterial: shading model {} has no bake step",
			static_cast<uint32_t>(material.shadingModel));
	}

	bool
	isBakedMapName(std::string_view fileName) noexcept
	{
		static constexpr std::array<std::string_view, c_Groups.size() + 1> c_Names = { {
			c_Groups[0].name,
			c_Groups[1].name,
			c_Groups[2].name,
			c_SurfaceSlotBakePrefix,
		} };
		return isBakedNameAmong(fileName, c_Names);
	}

	void
	stripAuthoringData(BMaterial& material)
	{
		const bool isPbr     = material.shadingModel == ShadingModel::kPbr;
		const bool isSurface = material.shadingModel == ShadingModel::kPbrSurface;

		// Checked before anything is cleared: a material that cannot be stripped must come out of
		// this call untouched, not half-stripped.
		if (isPbr)
		{
			const PbrParams& pbr = material.pbr;

			const bool hasRoutes = std::ranges::any_of(pbr.routes, [](const ChannelRoute& route) {
				return !route.texture.empty();
			});

			if (hasRoutes && pbr.baseColorTexture.empty() && pbr.ormTexture.empty() &&
			    pbr.normalTexture.empty())
			{
				throw std::runtime_error(
					"assetlib::stripAuthoringData: the material has never been baked; stripping "
					"its "
					"routes would leave nothing to render");
			}
		}

		if (isSurface)
		{
			for (const SurfaceTextureBinding& slot : material.surface.textures)
				core::throw_runtime_error_if(
					slotIsRouted(slot) && slot.bakedPath.empty(),
					"assetlib::stripAuthoringData: slot '{}' has never been baked; stripping its "
					"routes would leave nothing to render",
					slot.name);
		}

		if (isPbr)
		{
			material.pbr.routes      = {};
			material.pbr.routeStamps = {};
		}

		if (isSurface)
		{
			for (SurfaceTextureBinding& slot : material.surface.textures)
			{
				slot.routes      = {};
				slot.routeStamps = {};
			}
		}

		material.editorGraph.clear();

		// Unknown keys are authoring data until a build knows otherwise: a shipped tree carries
		// only what this build can interpret.
		material.extraJson = "{}";
	}

	void
	AssetStore::BakeMaterial(BMaterial& material, const CancelToken& cancel) const
	{
		bakeMaterial(
			material,
			{ .dataRoot = m_DataRoot, .textureDir = c_BakedTexturesDirectoryName },
			cancel);
	}

	void
	AssetStore::ResolveMaterialBake(BMaterial& material) const
	{
		bakeMaterial(
			material,
			{ .dataRoot = m_DataRoot, .textureDir = c_BakedTexturesDirectoryName, .write = false },
			{});
	}

	bool
	AssetStore::CanComposeSurfaceSlots(const BMaterial& material) const
	{
		// The host, deliberately, because it is where the compositor reads (SourceCache above):
		// on a packed mount the sources resolve through the archive but cannot be composited, and
		// a routed material there should have been stripped anyway.
		for (const SurfaceTextureBinding& slot : material.surface.textures)
			for (const ChannelRoute& route : slot.routes)
				if (!route.texture.empty() && stampOf(m_DataRoot / route.texture).size == 0)
					return false;
		return true;
	}

	ImageData
	AssetStore::ComposeSurfaceSlot(const BMaterial& material, std::string_view slotName) const
	{
		const auto slot = std::ranges::find_if(
			material.surface.textures,
			[&](const SurfaceTextureBinding& binding) { return binding.name == slotName; });
		core::throw_runtime_error_if(
			slot == material.surface.textures.end(),
			"assetlib::ComposeSurfaceSlot: material '{}' has no slot '{}'",
			material.name,
			slotName);

		const auto routes = std::span<const ChannelRoute>(slot->routes);
		core::throw_runtime_error_if(
			!anyRouted(routes),
			"assetlib::ComposeSurfaceSlot: slot '{}' routes nothing",
			slotName);

		SourceCache sources(m_DataRoot);

		const auto [width, height] = mapExtent(routes, sources);
		const Rgba8 composed       = compose(routes, c_SlotFallback, sources, width, height);

		return rgba8ToImage(composed, width, height, std::nullopt, false);
	}
}
