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

			// DXIL bytecode per shader, ready to bind into a PSO. Held as raw bytes
			// (not a slang blob) so it can come equally from a fresh compile or the
			// shader cache.
			std::unordered_map<IShader*, std::vector<std::byte>> entryPointCode;
		};

		// Builds a PSO's root signature, reflection, and DXIL. When cache is non-null
		// and holds an up-to-date entry for this shader composition, the entire slang
		// pipeline (source parse + codegen) is skipped; otherwise the result is
		// compiled and stored back into the cache.
		PipelineLayout
		BuildPipelineLayout(
			ID3D12Device*                   device,
			slang::ISession*                session,
			const ShaderCache*              cache,
			std::initializer_list<IShader*> shaders);
	}
}
