#include <assetlib/material_bake.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>

#include "bmesh_texture.h"

#include <stb_image_resize2.h>

namespace assetlib
{
	namespace
	{
		// One decoded source texture: tightly packed RGBA8 at its own resolution.
		using Rgba8 = std::vector<std::byte>;

		struct Source
		{
			Rgba8    pixels;
			uint32_t width  = 0;
			uint32_t height = 0;
		};

		// The three maps a material bakes to. Which routes feed each is not restated here: the run comes
		// from BMaterial.h, which owns the `routes` array and is the one place its layout is described.
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

		// `pixels` (srcW x srcH RGBA8) resampled to dstW x dstH. A no-op when they already match.
		Rgba8
		resample(const Rgba8& pixels, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
		{
			if (srcW == dstW && srcH == dstH)
				return pixels;

			Rgba8 out(static_cast<size_t>(dstW) * dstH * 4u);
			if (stbir_resize_uint8_linear(
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

		// Decodes every distinct source the material routes, each at its own resolution.
		std::unordered_map<std::string, Source>
		loadSources(const BMaterial& material, const std::filesystem::path& dataRoot)
		{
			auto sources = std::unordered_map<std::string, Source>();

			for (const ChannelRoute& route : material.routes)
			{
				if (route.texture.empty() || sources.contains(route.texture))
					continue;

				// Sources are read, not drawn: decode Basis to texels rather than to BC7 blocks.
				const ImageData image = loadKTX2(dataRoot / route.texture, Ktx2Decode::kRgba8);

				Source source;
				source.pixels = topMipRgba8(image, route.texture);
				source.width  = image.width;
				source.height = image.height;
				sources.emplace(route.texture, std::move(source));
			}

			if (sources.empty())
				throw std::runtime_error(
					"assetlib::bakeMaterial: the material routes no source textures");

			return sources;
		}

		bool
		groupIsRouted(const BMaterial& material, const Group& group)
		{
			for (size_t i = ChannelIndex(group.channels, 0);
			     i < ChannelIndex(group.channels, group.channels.count);
			     ++i)
				if (!material.routes[i].texture.empty())
					return true;
			return false;
		}

		/**
		 * Whether this group has to carry a real alpha channel.
		 *
		 * Base color is the only 4-channel group, so it is the only one with an alpha component at all
		 * -- ORM and normal have none. Whether that component matters is the material's authored alpha
		 * mode, *not* something inferred from the routes: an importer that wires all four channels of
		 * every texture out of habit would otherwise turn every material into a cutout.
		 */
		bool
		groupCarriesAlpha(const BMaterial& material, const Group& group)
		{
			return group.channels.count == c_BaseColorChannels.count &&
			       material.alphaMode == AlphaMode::kMask;
		}

		/**
		 * The block format this group bakes to, which is a property of the *material*, not of the group
		 * alone.
		 *
		 */
		Ktx2Compression
		groupCompression(const BMaterial& material, const Group& group)
		{
			return groupCarriesAlpha(material, group) ? Ktx2Compression::kBC7_RGBA :
			                                            group.compression;
		}

		// A group is sized to the largest source routed into *it*, so its output does not depend on any
		// texture outside the group -- which is what lets two materials share the baked file.
		std::pair<uint32_t, uint32_t>
		groupExtent(
			const BMaterial&                               material,
			const Group&                                   group,
			const std::unordered_map<std::string, Source>& sources)
		{
			uint32_t width  = 0;
			uint32_t height = 0;
			for (size_t i = ChannelIndex(group.channels, 0);
			     i < ChannelIndex(group.channels, group.channels.count);
			     ++i)
			{
				const ChannelRoute& route = material.routes[i];
				if (route.texture.empty())
					continue;

				const Source& source = sources.at(route.texture);
				width                = (std::max)(width, source.width);
				height               = (std::max)(height, source.height);
			}
			return { width, height };
		}

		/**
		 * Everything that determines a baked map's bytes, as a canonical string: the group, the target
		 * resolution and format, and the ordered (source, channel) pair feeding each of its components.
		 * Two materials that agree on all of this produce byte-identical output, so they should -- and
		 * do -- name the same file.
		 *
		 * `compression` is the *resolved* format, not `group.compression`: base color bakes to BC1 or
		 * BC7 depending on whether the material routes alpha, and the two must not converge on one file
		 * name.
		 */
		std::string
		bakeKey(
			const BMaterial& material,
			const Group&     group,
			uint32_t         width,
			uint32_t         height,
			Ktx2Compression  compression)
		{
			std::string key = std::string(group.name) + '|' + std::to_string(width) + 'x' +
			                  std::to_string(height) + '|' +
			                  std::to_string(static_cast<uint32_t>(compression));

			for (size_t i = ChannelIndex(group.channels, 0);
			     i < ChannelIndex(group.channels, group.channels.count);
			     ++i)
			{
				const ChannelRoute& route = material.routes[i];
				key += '|';
				key += route.texture;  // empty for an unrouted channel: the fallback is implied
				key += ':';
				key += route.texture.empty() ? std::to_string(group.fallback) :
				                               std::to_string(route.channel);
			}
			return key;
		}

		// FNV-1a. Not cryptographic: a collision would only alias two baked maps, and the inputs are
		// short, structured strings rather than adversarial ones.
		uint64_t
		hash64(std::string_view text)
		{
			uint64_t hash = 1469598103934665603ull;
			for (const char c : text)
			{
				hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
				hash *= 1099511628211ull;
			}
			return hash;
		}

		// Hex digits of the content hash in a baked map's name -- a uint64 printed as %016llx.
		constexpr size_t c_HashDigits = 16;

		constexpr std::string_view c_MapExtension = ".ktx2";

		std::string
		bakeFileName(const std::string& key, const Group& group)
		{
			char hex[c_HashDigits + 1] = {};
			std::snprintf(
				hex,
				sizeof(hex),
				"%016llx",
				static_cast<unsigned long long>(hash64(key)));
			return std::string(group.name) + '_' + hex + std::string(c_MapExtension);
		}

		/**
		 * Gathers a group's channels into a packed RGBA8 map at `width` x `height`. Destination
		 * component i takes its routed source's channel; an unrouted one takes the group's fallback, the
		 * value that makes the shader's factor alone drive that output. Components past the group's count
		 * stay 255 (BC1 ignores alpha; BC5 ignores B and A).
		 */
		Rgba8
		compose(
			const BMaterial&                               material,
			const Group&                                   group,
			const std::unordered_map<std::string, Source>& sources,
			uint32_t                                       width,
			uint32_t                                       height)
		{
			const size_t texels = static_cast<size_t>(width) * height;

			// Resampling is per (source, extent), so a source feeding two components of one group is
			// only scaled once.
			auto scaled = std::unordered_map<std::string, Rgba8>();

			Rgba8 out(texels * 4u, std::byte{ 0xFF });
			for (size_t component = 0; component < group.channels.count; ++component)
			{
				const ChannelRoute& route =
					material.routes[ChannelIndex(group.channels, component)];

				if (route.texture.empty())
				{
					for (size_t t = 0; t < texels; ++t)
						out[t * 4u + component] = static_cast<std::byte>(group.fallback);
					continue;
				}

				if (!scaled.contains(route.texture))
				{
					const Source& source = sources.at(route.texture);
					scaled.emplace(
						route.texture,
						resample(source.pixels, source.width, source.height, width, height));
				}

				const Rgba8& src     = scaled.at(route.texture);
				const size_t channel = (std::min)(static_cast<size_t>(route.channel), size_t{ 3 });
				for (size_t t = 0; t < texels; ++t) out[t * 4u + component] = src[t * 4u + channel];
			}
			return out;
		}

		// Whether `target` already holds this map's output: it exists and no source has been touched
		// since it was written. Re-encoding a 4K map costs seconds, and a shared map is asked for once
		// per material that references it.
		bool
		isUpToDate(
			const std::filesystem::path& target,
			const BMaterial&             material,
			const Group&                 group,
			const std::filesystem::path& dataRoot)
		{
			const SourceStamp stamp = stampOf(target);
			if (stamp.size == 0)
				return false;  // missing, or empty and so not a real map

			for (size_t i = ChannelIndex(group.channels, 0);
			     i < ChannelIndex(group.channels, group.channels.count);
			     ++i)
			{
				const ChannelRoute& route = material.routes[i];
				if (route.texture.empty())
					continue;

				const SourceStamp source = stampOf(dataRoot / route.texture);
				if (source.size == 0 || source.mtime > stamp.mtime)
					return false;
			}
			return true;
		}
	}

	void
	bakeMaterial(BMaterial& material, const MaterialBakeDesc& desc)
	{
		const std::unordered_map<std::string, Source> sources =
			loadSources(material, desc.dataRoot);

		const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
		std::error_code             ec;
		std::filesystem::create_directories(outDir, ec);

		// Which triplet field each group fills, in c_Groups order.
		std::string* const outputs[] = {
			&material.baseColorTexture,
			&material.ormTexture,
			&material.normalTexture,
		};

		for (size_t g = 0; g < c_Groups.size(); ++g)
		{
			const Group& group = c_Groups[g];

			// A group with nothing routed is not baked at all: an empty triplet entry makes the runtime
			// fall back to white / flat-normal, exactly what an all-default map would have been.
			if (!groupIsRouted(material, group))
				continue;

			const auto [width, height] = groupExtent(material, group, sources);

			const Ktx2Compression compression = groupCompression(material, group);

			const std::string name =
				bakeFileName(bakeKey(material, group, width, height, compression), group);
			const auto target = outDir / name;

			if (!isUpToDate(target, material, group, desc.dataRoot))
			{
				const std::optional<float> mipCutoff = groupCarriesAlpha(material, group) ?
				                                           std::optional(material.alphaCutoff) :
				                                           std::nullopt;

				const ImageData image = rgba8ToImage(
					compose(material, group, sources, width, height),
					width,
					height,
					mipCutoff);

				writeKTX2(image, target, group.srgb, compression);
			}

			// Recorded relative to the data root, not to the material file.
			*outputs[g] = (desc.textureDir / name).generic_string();
		}

		// Record what each source measured, so a later edit to one of them shows up as a stale bake.
		for (size_t i = 0; i < c_LooseChannelCount; ++i)
		{
			material.routeStamps[i] = material.routes[i].texture.empty() ?
			                              SourceStamp{} :
			                              stampOf(desc.dataRoot / material.routes[i].texture);
		}

		// The triplet now exists, so draw from it. The routes stay: they are how it gets re-baked.
		material.mode = MaterialMode::kBaked;
	}

	bool
	isBakedMapName(std::string_view fileName) noexcept
	{
		if (!fileName.ends_with(c_MapExtension))
			return false;
		fileName.remove_suffix(c_MapExtension.size());

		// The group name is itself allowed no underscore, so the last one is the hash separator.
		const size_t separator = fileName.rfind('_');
		if (separator == std::string_view::npos)
			return false;

		const std::string_view prefix = fileName.substr(0, separator);
		const std::string_view digits = fileName.substr(separator + 1);

		const auto isHex = [](char c) noexcept {
			return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		};

		if (digits.size() != c_HashDigits || !std::ranges::all_of(digits, isHex))
			return false;

		return std::ranges::any_of(c_Groups, [prefix](const Group& group) noexcept {
			return prefix == group.name;
		});
	}

	void
	stripAuthoringData(BMaterial& material)
	{
		const bool hasRoutes = std::ranges::any_of(material.routes, [](const ChannelRoute& route) {
			return !route.texture.empty();
		});

		if (hasRoutes && material.baseColorTexture.empty() && material.ormTexture.empty() &&
		    material.normalTexture.empty())
		{
			throw std::runtime_error(
				"assetlib::stripAuthoringData: the material has never been baked; stripping its "
				"routes would leave nothing to render");
		}

		material.routes      = {};
		material.routeStamps = {};
		material.editorGraph.clear();
		material.mode = MaterialMode::kBaked;
	}
}
