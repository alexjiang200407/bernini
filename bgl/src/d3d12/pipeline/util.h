#pragma once
#include "uniforms/UniformLayoutEntry.h"

namespace bgl
{
	class IShader;

	namespace pipeline_util
	{
		struct PipelineLayout
		{
			Slang::ComPtr<slang::IComponentType>                linkedProgram;
			wrl::ComPtr<ID3D12RootSignature>                    rootSignature;
			std::unordered_map<std::string, UniformLayoutEntry> uniformLayoutEntries;
		};

		PipelineLayout
		BuildPipelineLayout(
			ID3D12Device*                   device,
			slang::ISession*                session,
			std::initializer_list<IShader*> shaders);
	}
}
