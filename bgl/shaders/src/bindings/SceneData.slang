module SceneData;

import types.Vertex;

public struct SceneDataConstants
{
	DescriptorHandle<ByteAddressBuffer> vertexBuffer;

	public Vertex LoadVertex(uint vertexID)
	{
		return vertexBuffer.Load<Vertex>(vertexID * sizeof(Vertex));
	}
};

