

struct Payload
{
    int dummy;
};

groupshared Payload s_payload;

[numthreads(1, 1, 1)]
void AS_GBuffer(

	uint globalIdx : SV_DispatchThreadID)
{
    s_payload.dummy = 0;

    DispatchMesh(1, 1, 1, s_payload);
}
