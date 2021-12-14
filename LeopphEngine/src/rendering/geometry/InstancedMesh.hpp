#pragma once

#include "MeshData.hpp"
#include "../shaders/ShaderProgram.hpp"

#include <array>
#include <cstddef>
#include <memory>


namespace leopph::impl
{
	class InstancedMesh
	{
		public:
			InstancedMesh(MeshData& meshData, unsigned instanceBuffer);

			InstancedMesh(const InstancedMesh& other);
			InstancedMesh& operator=(const InstancedMesh& other);

			InstancedMesh(InstancedMesh&& other) noexcept;
			InstancedMesh& operator=(InstancedMesh&& other) noexcept;

			~InstancedMesh();

			bool operator==(const InstancedMesh& other) const;

			void DrawShaded(ShaderProgram& shader, std::size_t nextFreeTextureUnit, std::size_t instanceCount) const;
			void DrawDepth(std::size_t instanceCount) const;

			// Reload the InstancedMesh by rereading the data from its original MeshData source.
			void Update();


		private:
			// Decrements ref count and deletes GL resources if necessary.
			void Deinit() const;

			constexpr static std::size_t VERTEX_BUFFER{0ull};
			constexpr static std::size_t INDEX_BUFFER{1ull};


			MeshData* m_MeshDataSrc{nullptr};
			std::shared_ptr<Material> m_Material{nullptr};
			/* This is shared between copies of an original object.
			 * We don't have to worry about newly created objects because as long as a buffer name is used by at least one copy, GL will not hand out the same name twice.
			 * MUST NOT BE LEFT AS NULLPTR AFTER CONSTRUCTION! */
			std::shared_ptr<std::size_t> m_RefCount{nullptr};

			unsigned m_VertexArray{0};
			std::array<unsigned, 2> m_Buffers{0, 0};
			std::size_t m_VertexCount{0};
			std::size_t m_IndexCount{0};
	};
}
