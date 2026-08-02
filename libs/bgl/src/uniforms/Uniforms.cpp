#include "uniforms/Uniforms.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"
#include "uniforms/DescriptorHandle.h"

namespace bgl
{
	namespace detail
	{
		namespace
		{
			TraversalResult
			ReturnNullResult();
		}

		class UniformNullNode final : public UniformsNode
		{
		public:
			UniformNullNode() = default;

			TraversalResult
			Traverse(size_t, std::string_view) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t, uint32_t) override
			{
				return ReturnNullResult();
			}

			UniformType
			GetType() const override
			{
				return UniformType::kNull;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}

			size_t
			GetSize() const override
			{
				return 0;
			}
		};

		static UniformNullNode g_UniformNullNode;

		namespace
		{
			TraversalResult
			ReturnNullResult()
			{
				TraversalResult result{};
				result.node           = &g_UniformNullNode;
				result.absoluteOffset = 0;
				return result;
			}
		}

		class UniformValueNode final : public UniformsNode
		{
		public:
			explicit UniformValueNode(UniformValueType valueType) : m_ValueType(valueType) {}

			TraversalResult
			Traverse(size_t, std::string_view) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t, uint32_t) override
			{
				return ReturnNullResult();
			}

			UniformType
			GetType() const override
			{
				return UniformType::kValue;
			}

			UniformValueType
			GetValueType() const override
			{
				return m_ValueType;
			}

			size_t
			GetSize() const override
			{
				return ValueTypeSize(m_ValueType);
			}

		private:
			UniformValueType m_ValueType;
		};

		class UniformStructNode final : public UniformsNode
		{
		public:
			struct Member
			{
				std::string                   name;
				std::unique_ptr<UniformsNode> node;
				size_t                        offset;

				Member(
					std::string                   memberName,
					std::unique_ptr<UniformsNode> memberNode,
					size_t                        off) :
					name(std::move(memberName)), node(std::move(memberNode)), offset(off)
				{}

				Member(Member&&) noexcept = default;
				Member&
				operator=(Member&&) noexcept = default;
				Member(const Member&)        = delete;
				Member&
				operator=(const Member&) = delete;
			};

			explicit UniformStructNode(std::vector<Member> members, size_t totalSize) :
				m_Members(std::move(members)), m_TotalSize(totalSize)
			{}

			TraversalResult
			Traverse(size_t currentOffset, std::string_view member) override
			{
				for (const Member& m : m_Members)
				{
					if (m.name == member)
					{
						return { m.node.get(), currentOffset + m.offset };
					}
				}
				return ReturnNullResult();
			}

			// Members are addressed by declaration position (see c_HandleUniformMember).
			TraversalResult
			Traverse(size_t currentOffset, uint32_t idx) override
			{
				if (idx >= m_Members.size())
				{
					return ReturnNullResult();
				}
				const Member& m = m_Members[idx];
				return { m.node.get(), currentOffset + m.offset };
			}

			UniformType
			GetType() const override
			{
				return UniformType::kStruct;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}
			size_t
			GetSize() const override
			{
				return m_TotalSize;
			}

		private:
			std::vector<Member> m_Members;
			size_t              m_TotalSize;
		};

		class UniformArrayNode final : public UniformsNode
		{
		public:
			explicit UniformArrayNode(
				std::unique_ptr<UniformsNode> elementNode,
				size_t                        count,
				size_t                        stride) :
				m_ElementNode(std::move(elementNode)), m_Count(count), m_Stride(stride)
			{}

			UniformArrayNode(const UniformArrayNode&) noexcept = delete;
			UniformArrayNode(UniformArrayNode&&) noexcept      = delete;

			UniformArrayNode&
			operator=(const UniformArrayNode&) noexcept = delete;

			UniformArrayNode&
			operator=(UniformArrayNode&&) noexcept = delete;

			TraversalResult
			Traverse(size_t, std::string_view) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t currentOffset, uint32_t idx) override
			{
				if (idx >= m_Count)
					return ReturnNullResult();
				return { m_ElementNode.get(), currentOffset + idx * m_Stride };
			}

			UniformType
			GetType() const override
			{
				return UniformType::kArray;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}

			size_t
			GetSize() const override
			{
				return m_Count * m_Stride;
			}

			size_t
			GetCount() const
			{
				return m_Count;
			}

		private:
			std::unique_ptr<UniformsNode> m_ElementNode;
			size_t                        m_Count;
			size_t                        m_Stride;
		};

	}

	Uniforms::Accessor
	Uniforms::operator[](std::string_view name)
	{
		return Accessor(m_Buffer.data(), 0, m_Root.get())[name];
	}

	Uniforms::Accessor
	Uniforms::operator[](uint32_t idx)
	{
		return Accessor(m_Buffer.data(), 0, m_Root.get())[idx];
	}

	Uniforms::ConstAccessor
	Uniforms::operator[](std::string_view name) const
	{
		return ConstAccessor(m_Buffer.data(), 0, m_Root.get())[name];
	}

	Uniforms::ConstAccessor
	Uniforms::operator[](uint32_t idx) const
	{
		return ConstAccessor(m_Buffer.data(), 0, m_Root.get())[idx];
	}

	Uniforms::Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName)
	{
		gassert(pipeline != nullptr, "Pipeline pointer cannot be null");

		UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(cbufferName);

		gassert(entry.layout != nullptr, "Pipeline must have a valid uniform layout");

		m_Size           = entry.size;
		m_RootParamIndex = entry.rootParamIndex;
		m_Root           = BuildNode(*entry.layout);
		m_Buffer.resize(entry.size, std::byte{ 0 });
	}

	Uniforms::Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName)
	{
		gassert(pipeline != nullptr, "Pipeline pointer cannot be null");

		UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(cbufferName);

		gassert(entry.layout != nullptr, "Pipeline must have a valid uniform layout");

		m_Size           = entry.size;
		m_RootParamIndex = entry.rootParamIndex;
		m_Root           = BuildNode(*entry.layout);
		m_Buffer.resize(entry.size, std::byte{ 0 });
	}

	std::unique_ptr<detail::UniformsNode>
	Uniforms::BuildNode(const ReflectedLayout& layout)
	{
		switch (layout.kind)
		{
		case UniformType::kStruct:
		{
			std::vector<detail::UniformStructNode::Member> members;
			members.reserve(layout.fields.size());

			for (const ReflectedField& field : layout.fields)
			{
				members.emplace_back(field.name, BuildNode(field.layout), field.offset);
			}

			return std::make_unique<detail::UniformStructNode>(std::move(members), layout.size);
		}

		case UniformType::kArray:
		{
			gassert(layout.element.size() == 1, "Array layout must carry one element type");

			return std::make_unique<detail::UniformArrayNode>(
				BuildNode(layout.element.front()),
				layout.arrayCount,
				layout.arrayStride);
		}

		case UniformType::kValue:
			return std::make_unique<detail::UniformValueNode>(layout.valueType);

		case UniformType::kNull:
		default:
			gfatal("Unsupported reflected layout kind in push constants");
		}
	}
}
