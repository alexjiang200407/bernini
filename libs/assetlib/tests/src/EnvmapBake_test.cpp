#include <assetlib/envmap_bake.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

// Every case here pins an invariant that has already been violated in this codebase at least once,
// and every one of those failures was silent -- the render still looked plausible. docs/envmaps.md
// explains each; this file is what stops them coming back.
namespace
{
	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// The D3D cube convention, matching what envmap_bake.cpp emits.
	Vec3
	FaceDir(uint32_t face, uint32_t col, uint32_t row, uint32_t size)
	{
		const float u = (static_cast<float>(col) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;

		Vec3 d;
		switch (face)
		{
		case 0:
			d = { 1.0f, -v, -u };
			break;
		case 1:
			d = { -1.0f, -v, u };
			break;
		case 2:
			d = { u, 1.0f, v };
			break;
		case 3:
			d = { u, -1.0f, -v };
			break;
		case 4:
			d = { u, -v, 1.0f };
			break;
		default:
			d = { -u, -v, -1.0f };
			break;
		}

		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		return { d.x / len, d.y / len, d.z / len };
	}

	float
	TexelSolidAngle(uint32_t col, uint32_t row, uint32_t size)
	{
		const float u = (static_cast<float>(col) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const float d = 1.0f + u * u + v * v;
		return (4.0f / static_cast<float>(size * size)) / (d * std::sqrt(d));
	}

	const float*
	FaceMip(const ImageData& cube, uint32_t face, uint32_t mip)
	{
		const size_t index = static_cast<size_t>(face) * cube.mipLevels + mip;
		return reinterpret_cast<const float*>(cube.pixels.data() + cube.subresources[index].offset);
	}

	// Solid-angle-weighted mean luminance of one mip -- the quantity docs/envmaps.md requires to be
	// equal across a prefilter chain, because a normalized convolution moves energy but cannot make it.
	double
	MeanRadiance(const ImageData& cube, uint32_t mip)
	{
		const uint32_t size = std::max(1u, cube.width >> mip);
		double         sum  = 0.0;
		double         wsum = 0.0;

		for (uint32_t face = 0; face < 6; ++face)
		{
			const float* p = FaceMip(cube, face, mip);
			for (uint32_t row = 0; row < size; ++row)
			{
				for (uint32_t col = 0; col < size; ++col)
				{
					const float* t = p + (static_cast<size_t>(row) * size + col) * 4;
					const double w = TexelSolidAngle(col, row, size);
					sum += static_cast<double>(t[0] + t[1] + t[2]) / 3.0 * w;
					wsum += w;
				}
			}
		}
		return sum / wsum;
	}

	// Radiance-weighted mean direction: the environment's apparent light direction. This is what
	// detects an orientation error; no linear image metric can, because a peak far above the mean
	// makes every candidate orientation score alike.
	Vec3
	DominantDirection(const ImageData& cube, uint32_t mip = 0)
	{
		const uint32_t size   = std::max(1u, cube.width >> mip);
		double         acc[3] = { 0.0, 0.0, 0.0 };

		for (uint32_t face = 0; face < 6; ++face)
		{
			const float* p = FaceMip(cube, face, mip);
			for (uint32_t row = 0; row < size; ++row)
			{
				for (uint32_t col = 0; col < size; ++col)
				{
					const float* t = p + (static_cast<size_t>(row) * size + col) * 4;
					const double l = static_cast<double>(t[0] + t[1] + t[2]) / 3.0 *
					                 TexelSolidAngle(col, row, size);
					const Vec3   d = FaceDir(face, col, row, size);
					acc[0] += d.x * l;
					acc[1] += d.y * l;
					acc[2] += d.z * l;
				}
			}
		}

		const double len = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
		return { static_cast<float>(acc[0] / len),
			     static_cast<float>(acc[1] / len),
			     static_cast<float>(acc[2] / len) };
	}

	/** A cube map of one uniform radiance, for the cases where the exact answer is known. */
	ImageData
	ConstantCube(uint32_t size, float radiance)
	{
		ImageData out;
		out.width     = size;
		out.height    = size;
		out.mipLevels = 1;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		const size_t perFace = static_cast<size_t>(size) * size;
		out.pixels           = core::fixed_buffer<std::byte>(perFace * 6 * 4 * sizeof(float));

		auto*      px    = reinterpret_cast<float*>(out.pixels.data());
		const auto pitch = static_cast<uint64_t>(size) * 4 * sizeof(float);
		for (size_t t = 0; t < perFace * 6; ++t)
		{
			px[t * 4 + 0] = radiance;
			px[t * 4 + 1] = radiance;
			px[t * 4 + 2] = radiance;
			px[t * 4 + 3] = 1.0f;
		}
		for (uint32_t face = 0; face < 6; ++face)
			out.subresources.push_back({ face * perFace * 4 * sizeof(float), pitch, pitch * size });

		return out;
	}

	/** An equirectangular image, dark except for one bright texel at (u, v). */
	ImageData
	EquirectWithSpot(uint32_t width, uint32_t height, float u, float v, float radiance)
	{
		ImageData out;
		out.width     = width;
		out.height    = height;
		out.mipLevels = 1;
		out.arraySize = 1;
		out.isCubemap = false;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		const size_t texels = static_cast<size_t>(width) * height;
		out.pixels          = core::fixed_buffer<std::byte>(texels * 4 * sizeof(float));

		auto* px = reinterpret_cast<float*>(out.pixels.data());
		std::fill(px, px + texels * 4, 0.0f);
		for (size_t t = 0; t < texels; ++t) px[t * 4 + 3] = 1.0f;

		const auto col = std::min(static_cast<uint32_t>(u * static_cast<float>(width)), width - 1);
		const auto row =
			std::min(static_cast<uint32_t>(v * static_cast<float>(height)), height - 1);
		float* hit = px + (static_cast<size_t>(row) * width + col) * 4;
		hit[0] = hit[1] = hit[2] = radiance;

		const auto pitch = static_cast<uint64_t>(width) * 4 * sizeof(float);
		out.subresources.push_back({ 0, pitch, pitch * height });
		return out;
	}

	/**
	 * An equirectangular image, dark except for one bright row of latitude.
	 *
	 * A band rather than a single texel, because near a pole the cube's longitude sampling is sparse
	 * enough to miss one texel entirely -- which says nothing about the projection.
	 */
	ImageData
	EquirectWithBand(uint32_t width, uint32_t height, uint32_t row, float radiance)
	{
		ImageData out = EquirectWithSpot(width, height, 0.0f, 0.0f, 0.0f);

		auto* px = reinterpret_cast<float*>(out.pixels.data());
		for (uint32_t col = 0; col < width; ++col)
		{
			float* t = px + (static_cast<size_t>(row) * width + col) * 4;
			t[0] = t[1] = t[2] = radiance;
		}
		return out;
	}

	float
	Angle(Vec3 a, Vec3 b)
	{
		const float d = std::clamp(a.x * b.x + a.y * b.y + a.z * b.z, -1.0f, 1.0f);
		return static_cast<float>(std::acos(d) * 180.0 / 3.14159265358979323846);
	}
}

// The cube parameterization every other case leans on. A wrong solid angle would silently bias
// every mean and every SH coefficient in this file, so it is checked before anything uses it.
//
// Evaluating the weight at the texel centre is midpoint quadrature, not the exact solid angle, so
// the sum only approaches 4*pi -- second order, a quarter of the error per doubling. Convergence is
// the property worth pinning: a sign or exponent slip would break the trend, not just the tolerance.
TEST_CASE("cube texel solid angles converge to 4 pi", "[envmap]")
{
	constexpr double c_FourPi = 4.0 * 3.14159265358979323846;

	double previous = 1.0;
	for (const uint32_t size : { 8u, 16u, 32u, 64u })
	{
		double total = 0.0;
		for (uint32_t face = 0; face < 6; ++face)
			for (uint32_t row = 0; row < size; ++row)
				for (uint32_t col = 0; col < size; ++col) total += TexelSolidAngle(col, row, size);

		const double error = std::abs(total / c_FourPi - 1.0);
		INFO("size " << size << " total " << total << " error " << error);

		CHECK(error < previous * 0.5);  // second order, so comfortably better than halving
		previous = error;
	}

	CHECK(previous < 1e-4);  // 64^2 faces land within 0.01%
}

// Irradiance is stored as radiance/pi -- the cosine-weighted average incident radiance -- so a
// uniform environment must come back as its own value. Getting the pi factor wrong here is a flat
// 3.14x error in every diffuse surface in the engine, and it looks like a plain exposure mistake.
TEST_CASE("SH irradiance round-trips a constant environment", "[envmap][irradiance]")
{
	constexpr float c_Radiance = 0.75f;
	const ImageData iem        = IrradianceSh(ConstantCube(32, c_Radiance), 16);

	REQUIRE(iem.isCubemap);
	REQUIRE(iem.mipLevels == 1);
	REQUIRE(iem.width == 16);

	// Every texel, not just the mean: a constant environment has no direction, so any variation is
	// an error in the basis evaluation rather than in the projection.
	for (uint32_t face = 0; face < 6; ++face)
	{
		const float* p = FaceMip(iem, face, 0);
		for (uint32_t t = 0; t < 16 * 16; ++t)
			CHECK(p[t * 4] == Catch::Approx(c_Radiance).epsilon(0.02));
	}
}

// docs/envmaps.md invariant 3: iem's mean equals the environment's. Together with the case above
// this pins both the scale and the projection.
TEST_CASE("SH irradiance preserves the environment's mean", "[envmap][irradiance]")
{
	const ImageData src  = EquirectWithSpot(64, 32, 0.5f, 0.5f, 400.0f);
	const ImageData cube = EquirectToCube(src, 32);
	const ImageData iem  = IrradianceSh(cube, 16);

	CHECK(MeanRadiance(iem, 0) == Catch::Approx(MeanRadiance(cube, 0)).epsilon(0.05));
}

// The orientation case. A mirrored longitude still yields a plausible environment, just rotated 42
// degrees, and it shipped once precisely because nothing checked it. u = 0.5 must land on +Z and
// u = 0.75 on +X; those two together pin both the offset and the handedness.
TEST_CASE("equirect longitude maps to the expected cube direction", "[envmap][orientation]")
{
	struct Case
	{
		float       u;
		Vec3        expected;
		const char* what;
	};

	const Case cases[] = {
		{ 0.50f, { 0.0f, 0.0f, 1.0f }, "u=0.50 -> +Z" },
		{ 0.75f, { 1.0f, 0.0f, 0.0f }, "u=0.75 -> +X" },
		{ 0.00f, { 0.0f, 0.0f, -1.0f }, "u=0.00 -> -Z" },
		{ 0.25f, { -1.0f, 0.0f, 0.0f }, "u=0.25 -> -X" },
	};

	for (const Case& c : cases)
	{
		INFO(c.what);
		// On the equator, so latitude cannot alias into the result.
		const ImageData cube = EquirectToCube(EquirectWithSpot(128, 64, c.u, 0.5f, 500.0f), 64);
		CHECK(Angle(DominantDirection(cube), c.expected) < 5.0f);
	}
}

// Latitude, independently: v = 0 is +Y. A flipped v is the other half of an orientation bug and
// would leave the ground lighting the sky.
TEST_CASE("equirect latitude maps v=0 to +Y and v=1 to -Y", "[envmap][orientation]")
{
	const ImageData top = EquirectToCube(EquirectWithBand(128, 64, 0, 500.0f), 64);
	CHECK(Angle(DominantDirection(top), { 0.0f, 1.0f, 0.0f }) < 10.0f);

	const ImageData bottom = EquirectToCube(EquirectWithBand(128, 64, 63, 500.0f), 64);
	CHECK(Angle(DominantDirection(bottom), { 0.0f, -1.0f, 0.0f }) < 10.0f);
}

// docs/envmaps.md invariant 1, and the reason it is worth a test: a prefilter mip is a *normalized*
// weighted average, so blurring redistributes energy but cannot create it. Dividing by the sample
// count instead of the summed weight makes this climb, and the visible symptom -- a too-bright
// roughness-1 specular -- is easy to mistake for an exposure problem.
TEST_CASE("the prefilter chain does not gain energy with roughness", "[envmap][prefilter]")
{
	const ImageData cube = EquirectToCube(EquirectWithSpot(128, 64, 0.4f, 0.35f, 200.0f), 64);

	auto desc      = PrefilterDesc();
	desc.faceSize  = 64;  // a 7-mip chain needs at least this: 32 >> 6 would be 0
	desc.mipLevels = 7;
	desc.samples   = 256;

	auto            stats = PrefilterStats();
	const ImageData out   = PrefilterRadiance(cube, desc, &stats);

	REQUIRE(out.mipLevels == 7);
	REQUIRE(out.width == 64);
	CHECK(stats.samplesTaken > 0);

	const double base = MeanRadiance(out, 0);
	for (uint32_t mip = 1; mip < 7; ++mip)
	{
		INFO("mip " << mip);
		// Generous, because mip selection biases this a little by design (see the header); the
		// failure this guards against is a runaway climb, not the last fraction of a percent.
		CHECK(MeanRadiance(out, mip) / base < 1.10);
		CHECK(MeanRadiance(out, mip) / base > 0.90);
	}
}

// Roughness 0 is a delta lobe, so mip 0 must be the environment itself rather than a convolution
// of it -- this is what keeps a mirror surface reflecting the world and not a blur of it.
TEST_CASE("the prefilter's mip 0 reproduces the source", "[envmap][prefilter]")
{
	const ImageData cube = EquirectToCube(EquirectWithSpot(128, 64, 0.6f, 0.5f, 300.0f), 32);

	auto desc      = PrefilterDesc();
	desc.faceSize  = 64;
	desc.mipLevels = 7;
	desc.samples   = 64;

	const ImageData out = PrefilterRadiance(cube, desc, nullptr);

	CHECK(MeanRadiance(out, 0) == Catch::Approx(MeanRadiance(cube, 0)).epsilon(0.02));
	CHECK(Angle(DominantDirection(out, 0), DominantDirection(cube, 0)) < 2.0f);
}

// The seam case. A per-face bilinear clamp, or a resize that resamples each face independently,
// leaves adjacent faces disagreeing along a shared edge -- which reads on a mirror surface as
// bright lines tracing the cube's edges. A constant environment must stay exactly constant, border
// texels included, because a delta lobe over a uniform world has one answer everywhere.
TEST_CASE("the prefilter keeps a constant environment seamless", "[envmap][prefilter]")
{
	constexpr float c_Radiance = 1.25f;

	auto desc      = PrefilterDesc();
	desc.faceSize  = 16;
	desc.mipLevels = 5;
	desc.samples   = 64;

	const ImageData out = PrefilterRadiance(ConstantCube(32, c_Radiance), desc, nullptr);

	for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
	{
		const uint32_t size = std::max(1u, desc.faceSize >> mip);
		for (uint32_t face = 0; face < 6; ++face)
		{
			const float* p = FaceMip(out, face, mip);
			for (uint32_t t = 0; t < size * size; ++t)
			{
				INFO("mip " << mip << " face " << face << " texel " << t);
				REQUIRE(p[t * 4] == Catch::Approx(c_Radiance).epsilon(0.01));
			}
		}
	}
}

// A mip count other than 7 silently remaps roughness against the shader's MAX_REFLECTION_LOD, so
// the roughness of a given mip is part of the contract and not an implementation detail.
TEST_CASE("prefilter geometry is rejected when it cannot hold the chain", "[envmap][prefilter]")
{
	const ImageData cube = ConstantCube(8, 1.0f);

	auto desc      = PrefilterDesc();
	desc.faceSize  = 4;
	desc.mipLevels = 7;  // 4 >> 6 is 0
	CHECK_THROWS_AS(PrefilterRadiance(cube, desc, nullptr), std::runtime_error);

	desc.faceSize  = 32;
	desc.mipLevels = 0;
	CHECK_THROWS_AS(PrefilterRadiance(cube, desc, nullptr), std::runtime_error);

	desc.mipLevels = 7;
	desc.samples   = 0;
	CHECK_THROWS_AS(PrefilterRadiance(cube, desc, nullptr), std::runtime_error);
}

TEST_CASE("a non-float or non-cube source is rejected", "[envmap]")
{
	ImageData notCube = ConstantCube(8, 1.0f);
	notCube.isCubemap = false;
	CHECK_THROWS_AS(IrradianceSh(notCube, 8), std::runtime_error);

	ImageData wrongFormat = ConstantCube(8, 1.0f);
	wrongFormat.vkFormat  = VkFormat::R8G8B8A8_UNORM;
	CHECK_THROWS_AS(IrradianceSh(wrongFormat, 8), std::runtime_error);

	CHECK_THROWS_AS(EquirectToCube(ConstantCube(8, 1.0f), 8), std::runtime_error);
}
