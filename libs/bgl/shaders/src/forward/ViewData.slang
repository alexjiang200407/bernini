public struct ViewData
{
    public float4x4 viewProj;

    // Equal to viewProj on the first frame.
    public float4x4 prevViewProj;
};

public ConstantBuffer<ViewData> viewData;
