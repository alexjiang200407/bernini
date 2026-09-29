// Fills gOutput[i] with a PCG hash of (i ^ seed). crowd::HashFillReference is the CPU half and must
// stay the same function.

struct HashFillParams
{
    uint count;
    uint seed;
};

// Bound where Slang places them, which reflection reads back: a register each on D3D12, one
// [[buffer(N)]] space shared by both on Metal.
ConstantBuffer<HashFillParams> gParams;
RWStructuredBuffer<uint> gOutput;

uint Pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= gParams.count)
        return;

    gOutput[id.x] = Pcg(id.x ^ gParams.seed);
}
