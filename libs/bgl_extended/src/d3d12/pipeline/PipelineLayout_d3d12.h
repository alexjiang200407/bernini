#pragma once
#include "uniforms/UniformLayoutEntry.h"

namespace bgl
{
	class IShader;
	class ShaderCache;

	namespace pipeline_util
	{
		struct PipelineLayout
		{
			wrl::ComPtr<ID3D12RootSignature> rootSignature;
			UniformLayoutMap                 uniformLayoutEntries;

			// DXIL bytecode keyed by entry-point name. Held as raw bytes (not a slang
			// blob) so it can come equally from a fresh compile or the shader cache.
			core::str::unordered_str_map<std::vector<std::byte>> entryPointCode;
		};

		// Builds a PSO's root signature, reflection, and DXIL. Takes no Slang session: on a cache
		// hit nothing here may touch Slang, so the session is reached only through the shaders,
		// and only down the compile path.
		PipelineLayout
		BuildPipelineLayout(
			ID3D12Device*                   device,
			const ShaderCache*              cache,
			std::initializer_list<IShader*> shaders);
	}
}
