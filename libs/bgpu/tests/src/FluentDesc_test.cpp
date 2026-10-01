#include <bgpu/resource/Sampler.h>
#include <bgpu/types/RasterState.h>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

TEST_CASE("A desc chained on a temporary stays an rvalue", "[desc]")
{
	STATIC_CHECK(
		std::is_same_v<
			decltype(bgpu::SamplerDesc().SetMipBias(1.f).SetMaxAnisotropy(4.f)),
			bgpu::SamplerDesc&&>);
	STATIC_CHECK(
		std::is_same_v<decltype(bgpu::RasterState().SetDepthBias(1)), bgpu::RasterState&&>);
}

TEST_CASE("A desc chained on a named desc returns that desc", "[desc]")
{
	auto  desc    = bgpu::SamplerDesc();
	auto& chained = desc.SetMipBias(1.f).SetMaxAnisotropy(4.f);
	STATIC_CHECK(std::is_same_v<decltype(chained), bgpu::SamplerDesc&>);
	CHECK(&chained == &desc);
	CHECK(desc.mipBias == 1.f);
	CHECK(desc.maxAnisotropy == 4.f);
}

TEST_CASE("A state chain is still a constant expression", "[desc]")
{
	constexpr auto c_State = bgpu::RasterState().SetDepthBias(3).SetDepthClipEnable(true);
	STATIC_CHECK(c_State.depthBias == 3);
	STATIC_CHECK(c_State.depthClipEnable);
}
