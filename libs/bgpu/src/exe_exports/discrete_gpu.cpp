#include <cstdint>

// On a laptop with two GPUs, NVIDIA's and AMD's drivers read these from the *executable* at launch
// and put the discrete GPU first in the adapter order, the one CreateGpuContext takes. A library
// cannot carry them, so each executable that renders links the bgpu_discrete_gpu OBJECT library.
// NOLINTBEGIN(readability-identifier-naming): the names the drivers look up
extern "C"
{
	__declspec(dllexport) extern const uint32_t NvOptimusEnablement                  = 1;
	__declspec(dllexport) extern const int32_t  AmdPowerXpressRequestHighPerformance = 1;
}
// NOLINTEND(readability-identifier-naming)
