import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.common;

// Every program surface_registry.cpp generates for a registered surface, instantiated on the null
// surface. The generated ones exist only at runtime, so this is what puts the shapes through the
// build's DXIL validation (shaders/CMakeLists.txt compiles every entry point under programs/).
// Nothing loads it at runtime.

[shader("pixel")]
ForwardPSOut PSOpaque(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameOpaqueProgram<NullSurface>(input, isFrontFace);
}

[shader("pixel")]
ForwardPSOut PSAlphaTested(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameAlphaTestedProgram<NullSurface>(input, isFrontFace);
}

[shader("pixel")]
ForwardPSOut PSHashedAlpha(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameHashedAlphaProgram<NullSurface>(input, isFrontFace);
}

[shader("pixel")]
void PSDepthAlphaTested(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameAlphaTested<NullSurface>(input, isFrontFace);
}

[shader("pixel")]
void PSDepthHashedAlpha(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameHashedAlpha<NullSurface>(input, isFrontFace);
}

[shader("pixel")]
float4 PSBlended(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    return materialData.ShadeGameBlended<NullSurface>(input, isFrontFace);
}
