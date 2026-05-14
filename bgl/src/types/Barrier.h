#pragma once
#include <core/containers/enum_set.h>

namespace bgl
{
	enum class BarrierSyncFlag : uint32_t
	{
		kNone             = 0,
		kAllCommands      = 0x00000001,  // Sync with the entire GPU pipeline
		kCopy             = 0x00000002,  // Blit, CopyBufferRegion, CopyTextureRegion
		kResolve          = 0x00000004,  // MSAA resolve operations
		kInputAssembler   = 0x00000008,  // Index and Vertex buffer pulling
		kVertexShader     = 0x00000010,
		kPixelShader      = 0x00000020,
		kComputeShader    = 0x00000040,
		kRenderTarget     = 0x00000080,  // Output-Merger blending/color writes
		kDepthStencil     = 0x00000100,  // Early/Late depth testing
		kRayTracing       = 0x00000200,  // Acceleration structure build/trace rays
		kIndirectArgument = 0x00000400   // ExecuteIndirect / DrawIndirect dispatch
	};

	enum class BarrierAccessFlag : uint32_t
	{
		kNone             = 0,
		kIndexBuffer      = 0x00000001,  // Read-only index cache
		kVertexBuffer     = 0x00000002,  // Read-only vertex cache
		kConstantBuffer   = 0x00000004,  // Read-only constant/uniform cache
		kShaderResource   = 0x00000008,  // Read-only texture/buffer sampling (SRV)
		kUnorderedAccess  = 0x00000010,  // Read/Write random access (UAV)
		kRenderTarget     = 0x00000020,  // Write-heavy color target cache (RTV)
		kDepthWrite       = 0x00000040,  // Write depth-stencil hardware cache
		kDepthRead        = 0x00000080,  // Read-only depth cache (for depth testing/sampling)
		kIndirectArgument = 0x00000100,  // Read-only argument buffer cache
		kCopySource       = 0x00000200,  // Read-only copy cache
		kCopyDest         = 0x00000400,  // Write-only copy cache
		kAccelStructRead  = 0x00000800,  // Read-only raytracing AS cache
		kAccelStructWrite = 0x00001000   // Write-only raytracing AS cache
	};

	enum class BarrierLayout : uint32_t
	{
		kUndefined = 0,    // Discards old contents (perfect for clearing a new target)
		kCommon,           // General purpose / fallback state
		kPresent,          // Hand-off layout to the OS window compositor
		kGenericRead,      // Read-only layout optimized for multiple shader stages
		kShaderResource,   // Read-only texture layout optimized for samplers
		kUnorderedAccess,  // Read/Write compute layout (bypasses compression)
		kRenderTarget,     // Write-only layout optimized for the Output-Merger
		kDepthWrite,       // Layout optimized for hardware depth/stencil writing
		kDepthRead,        // Read-only depth layout (allows reading depth in shaders)
		kCopySource,       // Layout optimized for reading during a copy/blit
		kCopyDest          // Layout optimized for writing during a copy/blit
	};

	using BarrierSync   = core::enum_set<BarrierSyncFlag>;
	using BarrierAccess = core::enum_set<BarrierAccessFlag>;
}
