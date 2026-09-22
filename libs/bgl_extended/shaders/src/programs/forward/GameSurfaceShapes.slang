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
ForwardPSOut PSLitOpaque(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameLitOpaqueProgram<NullLitSurface>(input, isFrontFace);
}

[shader("pixel")]
ForwardPSOut PSLitAlphaTested(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameLitAlphaTestedProgram<NullLitSurface>(input, isFrontFace);
}

[shader("pixel")]
ForwardPSOut PSLitHashedAlpha(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return materialData.GameLitHashedAlphaProgram<NullLitSurface>(input, isFrontFace);
}

[shader("pixel")]
void PSLitDepthAlphaTested(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameLitAlphaTested<NullLitSurface>(input, isFrontFace);
}

[shader("pixel")]
void PSLitDepthHashedAlpha(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameLitHashedAlpha<NullLitSurface>(input, isFrontFace);
}

// The generated blend program's shape: a switch on the record's kind, an arm per surface -- the
// arm's function picked by the surface's contract -- and the engine's kinds as the fallthrough.
[shader("pixel")]
float4 PSTransparent(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    switch (uint(LoadMaterialKind(input.materialOffset)))
    {
    case 4u:
        return materialData.ShadeGameBlended<NullSurface>(input, isFrontFace);
    case 5u:
        return materialData.ShadeGameLitBlended<NullLitSurface>(input, isFrontFace);
    default:
        return materialData.ShadeBlendedEngineKind(input, isFrontFace);
    }
}
