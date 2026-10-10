#pragma once
#include <filesystem>
#include <string_view>

namespace bgl::test
{
	/**
	 * A water surface as a game writes one, reduced to what a test can read off a pixel: foam where
	 * the scene behind is nearer than `shore` along the view ray, the shallow colour where the
	 * ground is nearer than `shallow` below the surface, and the deep colour past it, each at its
	 * own opacity. It reads no light, so every colour reaches the screen unshaded.
	 */
	constexpr std::string_view c_ProbeWater = R"(import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.WaterSurfaceSource;

struct ProbeWaterParams
{
    [Color]
    [Default(0.0, 0.0, 1.0)]
    float3 deep;

    [Color]
    [Default(0.0, 1.0, 0.0)]
    float3 shallow;

    [Color]
    [Default(1.0, 1.0, 1.0)]
    float3 foam;

    [Default(0.5)]
    float shore;

    [Default(1.0)]
    float shallowDepth;

    [Default(1.0)]
    float opacity;
};

struct ProbeWater : IWaterSurfaceSource
{
    typealias MaterialParams = ProbeWaterParams;

    static float4 Shade<R : IWaterMaterialReader, L : ISurfaceLight>(R reader, L light, ProbeWaterParams params)
    {
        if (reader.ViewDepth() < params.shore)
        {
            return float4(params.foam, 1.0);
        }
        let body = reader.GroundDepth() < params.shallowDepth ? params.shallow : params.deep;
        return float4(body, params.opacity);
    }
};
)";

	/**
	 * A fresh directory under the suite's temp root holding ProbeWater and the suite's Unlit lit
	 * surface, in that slot order.
	 */
	[[nodiscard]] std::filesystem::path
	WaterSurfaceDir();
}
