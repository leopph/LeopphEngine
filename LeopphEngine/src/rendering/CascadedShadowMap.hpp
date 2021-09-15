#pragma once

#include "../events/DirShadowMapResChangedEvent.hpp"
#include "../events/EventReceiver.hpp"
#include "../math/Matrix.hpp"
#include "../math/Vector.hpp"
#include "shaders/DeferredDirLightShader.hpp"

#include <cstddef>
#include <vector>


namespace leopph::impl
{
	class CascadedShadowMap final : public EventReceiver<DirShadowMapResChangedEvent>
	{
	public:
		CascadedShadowMap();

		CascadedShadowMap(const CascadedShadowMap& other) = delete;
		CascadedShadowMap(CascadedShadowMap&& other) = delete;

		CascadedShadowMap& operator=(const CascadedShadowMap& other) = delete;
		CascadedShadowMap& operator=(CascadedShadowMap&& other) = delete;

		~CascadedShadowMap() override;

		void BindTextureForWriting(std::size_t cascadeIndex) const;
		void UnbindTextureFromWriting() const;

		// Returns the next available texture unit after binding
		[[nodiscard]] int BindTexturesForReading(const DeferredDirLightShader& shader, int texUnit);
		void UnbindTexturesFromReading() const;

		void Clear() const;

		[[nodiscard]] Matrix4 WorldToClipMatrix(std::size_t cascadeIndex, const Matrix4& cameraInverseMatrix, const Matrix4& lightViewMatrix) const;
		[[nodiscard]] Vector2 CascadeBounds(std::size_t cascadeIndex) const;


	private:
		unsigned m_Fbo;
		std::vector<unsigned> m_TexIds;
		int m_TexBindStartIndex;
		std::vector<Matrix4> m_ProjMatrices;

		void OnEventReceived(const DirShadowMapResChangedEvent& event) override;
		void Init(const std::vector<std::size_t>& resolutions);
		void Deinit();
	};
}