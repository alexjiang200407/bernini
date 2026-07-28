#include "assetlib/envmap_bake.h"
#include <assetlib_structs/ImageData.h>

#include <stb_image.h>

#include <core/glm.h>
#include <core/math.h>

namespace assetlib
{
	namespace
	{
		constexpr float c_Pi = static_cast<float>(core::c_Pi);

		/**
		 * Direction for the centre of texel (col, row) on `face`, in the D3D cube convention: u runs
		 * left to right, v runs top to bottom, both mapped to [-1, 1].
		 */
		glm::vec3
		faceTexelDir(uint32_t face, float col, float row, uint32_t size) noexcept
		{
			const float u = (col + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
			const float v = (row + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;

			switch (face)
			{
			case 0:
				return glm::normalize(glm::vec3(1.0f, -v, -u));
			case 1:
				return glm::normalize(glm::vec3(-1.0f, -v, u));
			case 2:
				return glm::normalize(glm::vec3(u, 1.0f, v));
			case 3:
				return glm::normalize(glm::vec3(u, -1.0f, -v));
			case 4:
				return glm::normalize(glm::vec3(u, -v, 1.0f));
			default:
				return glm::normalize(glm::vec3(-u, -v, -1.0f));
			}
		}

		struct FaceUv
		{
			uint32_t face = 0;
			float    u    = 0.0f;  // [0, 1]
			float    v    = 0.0f;  // [0, 1]
		};

		// Inverse of faceTexelDir: picks the face whose axis dominates, then projects onto it.
		FaceUv
		dirToFaceUv(glm::vec3 d) noexcept
		{
			const float ax = std::fabs(d.x);
			const float ay = std::fabs(d.y);
			const float az = std::fabs(d.z);

			FaceUv out;
			float  sc = 0.0f;
			float  tc = 0.0f;
			float  ma = 0.0f;

			if (ax >= ay && ax >= az)
			{
				out.face = d.x > 0.0f ? 0u : 1u;
				ma       = ax;
				sc       = d.x > 0.0f ? -d.z : d.z;
				tc       = -d.y;
			}
			else if (ay >= az)
			{
				out.face = d.y > 0.0f ? 2u : 3u;
				ma       = ay;
				sc       = d.x;
				tc       = d.y > 0.0f ? d.z : -d.z;
			}
			else
			{
				out.face = d.z > 0.0f ? 4u : 5u;
				ma       = az;
				sc       = d.z > 0.0f ? d.x : -d.x;
				tc       = -d.y;
			}

			out.u = (sc / ma + 1.0f) * 0.5f;
			out.v = (tc / ma + 1.0f) * 0.5f;
			return out;
		}

		struct CubeMip0
		{
			uint32_t               size = 0;
			std::vector<glm::vec3> faces[6];
		};

		// Solid angle of one texel of an n-wide cube face at (u, v), both in [-1, 1]. Summed over
		// every texel of all six faces this comes to 4*pi.
		float
		texelSolidAngle(float u, float v, uint32_t size) noexcept
		{
			const float d = 1.0f + u * u + v * v;
			return (4.0f / static_cast<float>(size * size)) / (d * std::sqrt(d));
		}

		CubeMip0
		readCubeMip0(const ImageData& source, const char* who)
		{
			if (!source.isCubemap)
				throw std::runtime_error(std::string(who) + ": source is not a cube map");
			if (source.vkFormat != VkFormat::R32G32B32A32_SFLOAT)
				throw std::runtime_error(std::string(who) + ": source must be R32G32B32A32_SFLOAT");
			if (source.width != source.height || source.width == 0)
				throw std::runtime_error(std::string(who) + ": source faces must be square");

			CubeMip0 out;
			out.size = source.width;

			for (uint32_t face = 0; face < 6; ++face)
			{
				// Subresources are face-major, mip-minor; only mip 0 of each face is read.
				const size_t index = static_cast<size_t>(face) * source.mipLevels;
				if (index >= source.subresources.size())
					throw std::runtime_error(std::string(who) + ": source is missing cube faces");

				const std::byte* src = source.pixels.data() + source.subresources[index].offset;

				out.faces[face].resize(static_cast<size_t>(out.size) * out.size);
				for (size_t t = 0; t < out.faces[face].size(); ++t)
				{
					float rgba[4] = {};
					std::memcpy(rgba, src + t * sizeof(float) * 4, sizeof(rgba));
					out.faces[face][t] = { rgba[0], rgba[1], rgba[2] };
				}
			}

			return out;
		}

		ImageData
		makeCubeImage(uint32_t faceSize, uint32_t mipLevels)
		{
			ImageData out;
			out.width     = faceSize;
			out.height    = faceSize;
			out.mipLevels = mipLevels;
			out.arraySize = 6;
			out.isCubemap = true;
			out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

			size_t total = 0;
			for (uint32_t mip = 0; mip < mipLevels; ++mip)
			{
				const uint32_t size = std::max(1u, faceSize >> mip);
				total += static_cast<size_t>(size) * size * 6;
			}
			out.pixels = core::fixed_buffer<std::byte>(total * sizeof(float) * 4);

			size_t offset = 0;
			for (uint32_t face = 0; face < 6; ++face)
			{
				for (uint32_t mip = 0; mip < mipLevels; ++mip)
				{
					const uint32_t size  = std::max(1u, faceSize >> mip);
					const auto     pitch = static_cast<uint64_t>(size) * sizeof(float) * 4;
					out.subresources.push_back({ offset, pitch, pitch * size });
					offset += static_cast<size_t>(pitch) * size;
				}
			}

			return out;
		}

		/**
		 * A float cube map and the mip pyramid built from it, addressed by direction.
		 *
		 * The pyramid exists so a GGX sample can read a level whose texel covers roughly the solid
		 * angle that sample represents; without it a wide lobe needs orders of magnitude more samples
		 * to stop sparkling.
		 */
		class CubePyramid
		{
		public:
			CubePyramid(const ImageData& source);

			[[nodiscard]] uint32_t
			BaseSize() const noexcept
			{
				return m_Levels.empty() ? 0u : m_Levels.front().size;
			}

			[[nodiscard]] glm::vec3
			Sample(glm::vec3 dir, float mip) const noexcept;

		private:
			struct Level
			{
				uint32_t               size = 0;
				std::vector<glm::vec3> faces[6];
			};

			[[nodiscard]] glm::vec3
			Texel(uint32_t level, uint32_t face, int32_t col, int32_t row) const noexcept;

			[[nodiscard]] glm::vec3
			SampleLevel(glm::vec3 dir, uint32_t level) const noexcept;

			std::vector<Level> m_Levels;
		};

		CubePyramid::CubePyramid(const ImageData& source)
		{
			CubeMip0 mip0 = readCubeMip0(source, "assetlib::prefilterRadiance");

			Level base;
			base.size = mip0.size;
			for (uint32_t face = 0; face < 6; ++face)
				base.faces[face] = std::move(mip0.faces[face]);
			m_Levels.push_back(std::move(base));

			while (m_Levels.back().size > 1)
			{
				const Level&   prev = m_Levels.back();
				const uint32_t half = prev.size / 2;

				Level next;
				next.size = half;
				for (uint32_t face = 0; face < 6; ++face)
				{
					next.faces[face].resize(static_cast<size_t>(half) * half);
					for (uint32_t row = 0; row < half; ++row)
					{
						for (uint32_t col = 0; col < half; ++col)
						{
							const size_t    s0 = static_cast<size_t>(row * 2) * prev.size + col * 2;
							const size_t    s1 = s0 + 1;
							const size_t    s2 = s0 + prev.size;
							const size_t    s3 = s2 + 1;
							const glm::vec3 sum = prev.faces[face][s0] + prev.faces[face][s1] +
							                      prev.faces[face][s2] + prev.faces[face][s3];
							next.faces[face][static_cast<size_t>(row) * half + col] = sum * 0.25f;
						}
					}
				}
				m_Levels.push_back(std::move(next));
			}
		}

		glm::vec3
		CubePyramid::Texel(uint32_t level, uint32_t face, int32_t col, int32_t row) const noexcept
		{
			const Level&  lvl  = m_Levels[level];
			const int32_t size = static_cast<int32_t>(lvl.size);

			// A tap that leaves the face belongs to a neighbour. Re-deriving its direction and
			// re-resolving the face is what keeps the filter continuous across a cube seam -- exactly
			// the property a per-face clamp destroys.
			if (col < 0 || row < 0 || col >= size || row >= size)
			{
				const glm::vec3 dir =
					faceTexelDir(face, static_cast<float>(col), static_cast<float>(row), lvl.size);
				const FaceUv uv = dirToFaceUv(dir);
				const auto   c  = static_cast<int32_t>(uv.u * static_cast<float>(size));
				const auto   r  = static_cast<int32_t>(uv.v * static_cast<float>(size));
				return lvl.faces[uv.face]
				                [static_cast<size_t>(std::clamp(r, 0, size - 1)) * lvl.size +
				                 static_cast<size_t>(std::clamp(c, 0, size - 1))];
			}

			return lvl.faces[face][static_cast<size_t>(row) * lvl.size + static_cast<size_t>(col)];
		}

		glm::vec3
		CubePyramid::SampleLevel(glm::vec3 dir, uint32_t level) const noexcept
		{
			const Level& lvl  = m_Levels[level];
			const FaceUv uv   = dirToFaceUv(dir);
			const float  size = static_cast<float>(lvl.size);

			const float x  = uv.u * size - 0.5f;
			const float y  = uv.v * size - 0.5f;
			const float fx = std::floor(x);
			const float fy = std::floor(y);
			const auto  x0 = static_cast<int32_t>(fx);
			const auto  y0 = static_cast<int32_t>(fy);
			const float tx = x - fx;
			const float ty = y - fy;

			const glm::vec3 c00 = Texel(level, uv.face, x0, y0);
			const glm::vec3 c10 = Texel(level, uv.face, x0 + 1, y0);
			const glm::vec3 c01 = Texel(level, uv.face, x0, y0 + 1);
			const glm::vec3 c11 = Texel(level, uv.face, x0 + 1, y0 + 1);

			const glm::vec3 top = c00 * (1.0f - tx) + c10 * tx;
			const glm::vec3 bot = c01 * (1.0f - tx) + c11 * tx;
			return top * (1.0f - ty) + bot * ty;
		}

		glm::vec3
		CubePyramid::Sample(glm::vec3 dir, float mip) const noexcept
		{
			const auto  maxLevel = static_cast<float>(m_Levels.size() - 1);
			const float clamped  = std::clamp(mip, 0.0f, maxLevel);
			const auto  lo       = static_cast<uint32_t>(clamped);
			const auto  hi       = static_cast<uint32_t>(std::min(clamped + 1.0f, maxLevel));
			const float t        = clamped - static_cast<float>(lo);

			const glm::vec3 a = SampleLevel(dir, lo);
			if (lo == hi || t <= 0.0f)
				return a;
			return a * (1.0f - t) + SampleLevel(dir, hi) * t;
		}

		// Van der Corput radical inverse, base 2 -- the second dimension of a Hammersley set.
		float
		radicalInverseBase2(uint32_t bits) noexcept
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return static_cast<float>(bits) * 2.3283064365386963e-10f;
		}

		// GGX / Trowbridge-Reitz normal distribution.
		float
		distributionGgx(float nDotH, float roughness) noexcept
		{
			const float a  = roughness * roughness;
			const float a2 = a * a;
			const float d  = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
			return a2 / std::max(c_Pi * d * d, 1e-8f);
		}

		// A half-vector drawn from the GGX lobe about `n`, distributed by the NDF.
		glm::vec3
		importanceSampleGgx(float u1, float u2, glm::vec3 n, float roughness) noexcept
		{
			const float a = roughness * roughness;

			const float phi      = 2.0f * c_Pi * u1;
			const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
			const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

			const glm::vec3 h = { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };

			const glm::vec3 up      = std::fabs(n.z) < 0.999f ? glm::vec3{ 0.0f, 0.0f, 1.0f } :
			                                                    glm::vec3{ 1.0f, 0.0f, 0.0f };
			const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
			const glm::vec3 bitan   = glm::cross(n, tangent);

			return glm::normalize(tangent * h.x + bitan * h.y + n * h.z);
		}
	}

	ImageData
	loadRadianceHdr(const std::filesystem::path& path)
	{
		int width    = 0;
		int height   = 0;
		int channels = 0;

		float* data = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
		if (data == nullptr)
			throw std::runtime_error(
				"assetlib::loadRadianceHdr: cannot decode '" + path.string() +
				"': " + stbi_failure_reason());

		ImageData out;
		out.width     = static_cast<uint32_t>(width);
		out.height    = static_cast<uint32_t>(height);
		out.mipLevels = 1;
		out.arraySize = 1;
		out.isCubemap = false;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		const size_t bytes = static_cast<size_t>(width) * height * 4 * sizeof(float);
		out.pixels         = core::fixed_buffer<std::byte>(bytes);
		std::memcpy(out.pixels.data(), data, bytes);
		stbi_image_free(data);

		const auto pitch = static_cast<uint64_t>(width) * 4 * sizeof(float);
		out.subresources.push_back({ 0, pitch, pitch * static_cast<uint64_t>(height) });
		return out;
	}

	ImageData
	equirectToCube(const ImageData& equirect, uint32_t faceSize)
	{
		if (faceSize == 0)
			throw std::runtime_error("assetlib::equirectToCube: faceSize must be > 0");
		if (equirect.isCubemap || equirect.vkFormat != VkFormat::R32G32B32A32_SFLOAT)
			throw std::runtime_error(
				"assetlib::equirectToCube: source must be a 2D R32G32B32A32_SFLOAT image");
		if (equirect.subresources.empty())
			throw std::runtime_error("assetlib::equirectToCube: source has no pixels");

		const auto* src = reinterpret_cast<const float*>(
			equirect.pixels.data() + equirect.subresources.front().offset);
		const int32_t srcW = static_cast<int32_t>(equirect.width);
		const int32_t srcH = static_cast<int32_t>(equirect.height);

		// Longitude wraps, latitude clamps.
		auto fetch = [&](int32_t x, int32_t y) noexcept -> glm::vec3 {
			const int32_t cx = ((x % srcW) + srcW) % srcW;
			const int32_t cy = std::clamp(y, 0, srcH - 1);
			const float*  p  = src + (static_cast<size_t>(cy) * srcW + cx) * 4;
			return { p[0], p[1], p[2] };
		};

		ImageData out;
		out.width     = faceSize;
		out.height    = faceSize;
		out.mipLevels = 1;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		const size_t perFace = static_cast<size_t>(faceSize) * faceSize;
		out.pixels           = core::fixed_buffer<std::byte>(perFace * 6 * 4 * sizeof(float));

		const auto pitch = static_cast<uint64_t>(faceSize) * 4 * sizeof(float);
		for (uint32_t face = 0; face < 6; ++face)
			out.subresources.push_back(
				{ face * perFace * 4 * sizeof(float), pitch, pitch * faceSize });

		for (uint32_t face = 0; face < 6; ++face)
		{
			auto* dst = reinterpret_cast<float*>(out.pixels.data() + out.subresources[face].offset);

			for (uint32_t row = 0; row < faceSize; ++row)
			{
				for (uint32_t col = 0; col < faceSize; ++col)
				{
					const glm::vec3 d = faceTexelDir(
						face,
						static_cast<float>(col),
						static_cast<float>(row),
						faceSize);

					// atan2(x, z), not atan2(x, -z): the latter mirrors longitude, which lands the
					// environment 42 degrees off the orientation CMFT produces from the same source.
					const float u = 0.5f + std::atan2(d.x, d.z) / (2.0f * c_Pi);
					const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / c_Pi;

					const float x  = u * static_cast<float>(srcW) - 0.5f;
					const float y  = v * static_cast<float>(srcH) - 0.5f;
					const float fx = std::floor(x);
					const float fy = std::floor(y);
					const auto  x0 = static_cast<int32_t>(fx);
					const auto  y0 = static_cast<int32_t>(fy);
					const float tx = x - fx;
					const float ty = y - fy;

					const glm::vec3 top = fetch(x0, y0) * (1.0f - tx) + fetch(x0 + 1, y0) * tx;
					const glm::vec3 bot =
						fetch(x0, y0 + 1) * (1.0f - tx) + fetch(x0 + 1, y0 + 1) * tx;
					const glm::vec3 c = top * (1.0f - ty) + bot * ty;

					const size_t t = (static_cast<size_t>(row) * faceSize + col) * 4;
					dst[t + 0]     = c.x;
					dst[t + 1]     = c.y;
					dst[t + 2]     = c.z;
					dst[t + 3]     = 1.0f;
				}
			}
		}

		return out;
	}

	ImageData
	irradianceSh(const ImageData& source, uint32_t faceSize)
	{
		if (faceSize == 0)
			throw std::runtime_error("assetlib::irradianceSh: faceSize must be > 0");

		const CubeMip0 src = readCubeMip0(source, "assetlib::irradianceSh");

		// L[i] are the projections of incident radiance onto the 9 real SH basis functions of
		// order 3, ordered l=0; l=1 (m=-1,0,1); l=2 (m=-2,-1,0,1,2).
		glm::vec3 coeff[9] = {};

		for (uint32_t face = 0; face < 6; ++face)
		{
			for (uint32_t row = 0; row < src.size; ++row)
			{
				for (uint32_t col = 0; col < src.size; ++col)
				{
					const float u =
						(static_cast<float>(col) + 0.5f) / static_cast<float>(src.size) * 2.0f -
						1.0f;
					const float v =
						(static_cast<float>(row) + 0.5f) / static_cast<float>(src.size) * 2.0f -
						1.0f;

					const glm::vec3 d = faceTexelDir(
						face,
						static_cast<float>(col),
						static_cast<float>(row),
						src.size);
					const float     dw = texelSolidAngle(u, v, src.size);
					const glm::vec3 l =
						src.faces[face][static_cast<size_t>(row) * src.size + col] * dw;

					const float basis[9] = {
						0.282095f,                              // Y00
						0.488603f * d.y,                        // Y1-1
						0.488603f * d.z,                        // Y10
						0.488603f * d.x,                        // Y11
						1.092548f * d.x * d.y,                  // Y2-2
						1.092548f * d.y * d.z,                  // Y2-1
						0.315392f * (3.0f * d.z * d.z - 1.0f),  // Y20
						1.092548f * d.x * d.z,                  // Y21
						0.546274f * (d.x * d.x - d.y * d.y),    // Y22
					};

					for (int i = 0; i < 9; ++i) coeff[i] = coeff[i] + l * basis[i];
				}
			}
		}

		// Ramamoorthi & Hanrahan's clamped-cosine convolution, then / pi: the raw form yields
		// irradiance (pi for a unit-radiance environment) where the shader wants the average
		// incident radiance, so that a constant environment round-trips to its own value.
		constexpr float c_C1    = 0.429043f;
		constexpr float c_C2    = 0.511664f;
		constexpr float c_C3    = 0.743125f;
		constexpr float c_C4    = 0.886227f;
		constexpr float c_C5    = 0.247708f;
		constexpr float c_InvPi = 1.0f / c_Pi;

		ImageData out = makeCubeImage(faceSize, 1);

		for (uint32_t face = 0; face < 6; ++face)
		{
			auto* dst = reinterpret_cast<float*>(out.pixels.data() + out.subresources[face].offset);

			for (uint32_t row = 0; row < faceSize; ++row)
			{
				for (uint32_t col = 0; col < faceSize; ++col)
				{
					const glm::vec3 n = faceTexelDir(
						face,
						static_cast<float>(col),
						static_cast<float>(row),
						faceSize);

					const glm::vec3 e =
						coeff[8] * (c_C1 * (n.x * n.x - n.y * n.y)) +
						coeff[6] * (c_C3 * n.z * n.z) + coeff[0] * c_C4 + coeff[6] * -c_C5 +
						coeff[4] * (2.0f * c_C1 * n.x * n.y) +
						coeff[7] * (2.0f * c_C1 * n.x * n.z) +
						coeff[5] * (2.0f * c_C1 * n.y * n.z) + coeff[3] * (2.0f * c_C2 * n.x) +
						coeff[1] * (2.0f * c_C2 * n.y) + coeff[2] * (2.0f * c_C2 * n.z);

					const size_t t = (static_cast<size_t>(row) * faceSize + col) * 4;
					dst[t + 0]     = std::max(0.0f, e.x * c_InvPi);
					dst[t + 1]     = std::max(0.0f, e.y * c_InvPi);
					dst[t + 2]     = std::max(0.0f, e.z * c_InvPi);
					dst[t + 3]     = 1.0f;
				}
			}
		}

		return out;
	}

	namespace
	{
		/**
		 * Convolves one cube face with the GGX lobe at a fixed roughness, writing RGBA floats.
		 *
		 * Shared by the prefilter chain, which calls it once per mip with the roughness that mip
		 * stands for, and by the skybox blur, which calls it once. Returns the samples taken.
		 *
		 * Roughness 0 is a delta lobe, so it degenerates to a resample rather than an integral.
		 */
		uint64_t
		convolveFace(
			const CubePyramid& pyramid,
			uint32_t           face,
			uint32_t           size,
			float              roughness,
			uint32_t           samples,
			float              saTexel,
			float*             dst)
		{
			const auto sampleCount = static_cast<float>(samples);
			uint64_t   taken       = 0;

			for (uint32_t row = 0; row < size; ++row)
			{
				for (uint32_t col = 0; col < size; ++col)
				{
					const glm::vec3 n =
						faceTexelDir(face, static_cast<float>(col), static_cast<float>(row), size);

					auto sum = glm::vec3(0.0f);

					if (roughness <= 0.0f)
					{
						sum = pyramid.Sample(n, 0.0f);
					}
					else
					{
						float weight = 0.0f;
						for (uint32_t i = 0; i < samples; ++i)
						{
							const float u1 = static_cast<float>(i) / sampleCount;
							const float u2 = radicalInverseBase2(i);

							const glm::vec3 h     = importanceSampleGgx(u1, u2, n, roughness);
							const float     nDotH = glm::dot(n, h);
							// V is taken as N, the standard split-sum prefilter assumption.
							const glm::vec3 l     = glm::normalize(h * (2.0f * nDotH) + n * -1.0f);
							const float     nDotL = glm::dot(n, l);
							if (nDotL <= 0.0f)
								continue;

							const float d   = distributionGgx(nDotH, roughness);
							const float pdf = d * nDotH / (4.0f * std::max(nDotH, 1e-4f)) + 1e-4f;
							const float saSample = 1.0f / (sampleCount * pdf + 1e-4f);
							const float mip      = 0.5f * std::log2(saSample / saTexel);

							sum = sum + pyramid.Sample(l, mip) * nDotL;
							weight += nDotL;
							++taken;
						}

						// By the summed weight, never by the sample count -- see the header.
						sum = weight > 0.0f ? sum * (1.0f / weight) : pyramid.Sample(n, 0.0f);
					}

					const size_t t = (static_cast<size_t>(row) * size + col) * 4;
					dst[t + 0]     = sum.x;
					dst[t + 1]     = sum.y;
					dst[t + 2]     = sum.z;
					dst[t + 3]     = 1.0f;
				}
			}

			return taken;
		}
	}

	ImageData
	blurCube(
		const ImageData& source,
		uint32_t         faceSize,
		float            roughness,
		uint32_t         samples,
		uint32_t         threads)
	{
		if (faceSize == 0)
			throw std::runtime_error("assetlib::blurCube: faceSize must be > 0");
		if (samples == 0)
			throw std::runtime_error("assetlib::blurCube: samples must be > 0");

		const CubePyramid pyramid(source);
		ImageData         out = makeCubeImage(faceSize, 1);

		const auto  srcSize = static_cast<float>(pyramid.BaseSize());
		const float saTexel = 4.0f * c_Pi / (6.0f * srcSize * srcSize);

		const uint32_t threadCount =
			threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency());

		std::atomic<uint32_t> nextFace{ 0 };

		auto worker = [&]() {
			for (;;)
			{
				const uint32_t face = nextFace.fetch_add(1);
				if (face >= 6)
					return;

				auto* dst =
					reinterpret_cast<float*>(out.pixels.data() + out.subresources[face].offset);
				convolveFace(pyramid, face, faceSize, roughness, samples, saTexel, dst);
			}
		};

		std::vector<std::thread> pool;
		pool.reserve(threadCount);
		for (uint32_t i = 0; i < threadCount; ++i) pool.emplace_back(worker);
		for (std::thread& t : pool) t.join();

		return out;
	}

	float
	exposureFor(const ImageData& irradiance)
	{
		const CubeMip0 map = readCubeMip0(irradiance, "assetlib::exposureFor");

		double sum  = 0.0;
		double wsum = 0.0;
		for (uint32_t face = 0; face < 6; ++face)
		{
			for (uint32_t row = 0; row < map.size; ++row)
			{
				for (uint32_t col = 0; col < map.size; ++col)
				{
					const float u =
						(static_cast<float>(col) + 0.5f) / static_cast<float>(map.size) * 2.0f -
						1.0f;
					const float v =
						(static_cast<float>(row) + 0.5f) / static_cast<float>(map.size) * 2.0f -
						1.0f;

					// Cube texels do not subtend equal solid angles, so an unweighted mean would
					// over-count the corners and read the environment as brighter than it is.
					const double     w = texelSolidAngle(u, v, map.size);
					const glm::vec3& c = map.faces[face][static_cast<size_t>(row) * map.size + col];
					sum += static_cast<double>(c.x + c.y + c.z) / 3.0 * w;
					wsum += w;
				}
			}
		}

		const double mean = wsum > 0.0 ? sum / wsum : 0.0;

		// 0.96 is the reflectance of an 18% grey card under this convention; see docs/envmaps.md.
		const double reflected = 0.96 * mean * 0.18;
		return reflected > 1e-6 ? static_cast<float>(0.18 / reflected) : 1.0f;
	}

	ImageData
	prefilterRadiance(const ImageData& source, const PrefilterDesc& desc, PrefilterStats* stats)
	{
		if (desc.faceSize == 0 || desc.mipLevels == 0)
			throw std::runtime_error(
				"assetlib::prefilterRadiance: faceSize and mipLevels must be > 0");
		if (desc.samples == 0)
			throw std::runtime_error("assetlib::prefilterRadiance: samples must be > 0");
		if ((desc.faceSize >> (desc.mipLevels - 1)) == 0)
			throw std::runtime_error(
				"assetlib::prefilterRadiance: faceSize is too small for that many mips");

		const auto start = std::chrono::steady_clock::now();

		const CubePyramid pyramid(source);

		ImageData out = makeCubeImage(desc.faceSize, desc.mipLevels);

		size_t total = 0;
		for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
		{
			const uint32_t size = std::max(1u, desc.faceSize >> mip);
			total += static_cast<size_t>(size) * size * 6;
		}

		const uint32_t threadCount =
			desc.threads != 0 ? desc.threads : std::max(1u, std::thread::hardware_concurrency());

		std::atomic<uint64_t> samplesTaken{ 0 };
		std::atomic<uint32_t> nextJob{ 0 };

		struct Job
		{
			uint32_t face = 0;
			uint32_t mip  = 0;
		};

		std::vector<Job> jobs;
		for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
			for (uint32_t face = 0; face < 6; ++face) jobs.push_back({ face, mip });

		const float srcSize = static_cast<float>(pyramid.BaseSize());
		const float saTexel = 4.0f * c_Pi / (6.0f * srcSize * srcSize);

		auto worker = [&]() {
			for (;;)
			{
				const uint32_t jobIndex = nextJob.fetch_add(1);
				if (jobIndex >= jobs.size())
					return;

				const Job      job  = jobs[jobIndex];
				const uint32_t size = std::max(1u, desc.faceSize >> job.mip);
				const float    roughness =
					desc.mipLevels > 1 ?
						static_cast<float>(job.mip) / static_cast<float>(desc.mipLevels - 1) :
						0.0f;

				const size_t subIndex = static_cast<size_t>(job.face) * desc.mipLevels + job.mip;
				auto*        dst =
					reinterpret_cast<float*>(out.pixels.data() + out.subresources[subIndex].offset);

				samplesTaken.fetch_add(
					convolveFace(pyramid, job.face, size, roughness, desc.samples, saTexel, dst));
			}
		};

		std::vector<std::thread> pool;
		pool.reserve(threadCount);
		for (uint32_t i = 0; i < threadCount; ++i) pool.emplace_back(worker);
		for (std::thread& t : pool) t.join();

		if (stats != nullptr)
		{
			stats->seconds =
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			stats->texelsWritten = total;
			stats->samplesTaken  = samplesTaken.load();
		}

		return out;
	}
}
