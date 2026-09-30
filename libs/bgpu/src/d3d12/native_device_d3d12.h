#pragma once

#include <cstdint>

// Declared, not defined: a caller that dereferences one includes <directx/d3d12.h> itself.
struct ID3D12Device;
struct ID3D12PipelineState;
struct ID3D12RootSignature;

// include-cleaner wants the defining header for the declarations above.
// NOLINTBEGIN(misc-include-cleaner)
namespace bgpu
{
	class GpuContext;

	/**
	 * The D3D12 device behind a context, for the RHI's own objects; an owner asks its IDevice
	 * (`GetNativeObject(kD3D12Device)`). Borrowed: the context holds the reference.
	 */
	ID3D12Device*
	GetD3d12Device(const GpuContext& context) noexcept;

	/**
	 * A pipeline state some owner of the context already built, keyed by the root signature it was
	 * created against and the owner's `identity` for the rest of its description: bytecode and
	 * fixed state, the identity its driver pipeline library is keyed by. Shared because a PSO is
	 * immutable and the device's, and a new one is not free: under GPU-based validation the debug
	 * layer patches every new PSO on first use, per object.
	 *
	 * @return an added reference the caller releases, or null on a miss.
	 */
	[[nodiscard]] ID3D12PipelineState*
	FindPipelineState(
		const GpuContext&    context,
		ID3D12RootSignature* rootSignature,
		uint64_t             identity) noexcept;

	/**
	 * Offers `pipelineState` under the same key as FindPipelineState; the first one offered for a
	 * key is kept. The context holds it and its root signature until the context is destroyed.
	 * Safe from any thread: pipelines are built in parallel.
	 */
	void
	SharePipelineState(
		const GpuContext&    context,
		ID3D12RootSignature* rootSignature,
		uint64_t             identity,
		ID3D12PipelineState* pipelineState) noexcept;
}
// NOLINTEND(misc-include-cleaner)
