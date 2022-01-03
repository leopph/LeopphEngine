#include "DeferredRenderer.hpp"

#include "../../components/Camera.hpp"
#include "../../components/lighting/AmbientLight.hpp"
#include "../../config/Settings.hpp"
#include "../../data/DataManager.hpp"
#include "../../math/LeopphMath.hpp"
#include "../../math/Matrix.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <numeric>


namespace leopph::internal
{
	DeferredRenderer::DeferredRenderer() :
		m_ShadowShader{
			{
				{ShaderFamily::ShadowMapVertSrc, ShaderType::Vertex}
			}
		},
		m_CubeShadowShader{
			{
				{ShaderFamily::CubeShadowMapVertSrc, ShaderType::Vertex},
				{ShaderFamily::CubeShadowMapGeomSrc, ShaderType::Geometry},
				{ShaderFamily::CubeShadowMapFragSrc, ShaderType::Fragment}
			}
		},
		m_GeometryShader{
			{
				{ShaderFamily::GPassObjectVertSrc, ShaderType::Vertex},
				{ShaderFamily::GPassObjectFragSrc, ShaderType::Fragment}
			}
		},
		m_LightShader{
			{
				{ShaderFamily::LightPassVertSrc, ShaderType::Vertex},
				{ShaderFamily::LightPassFragSrc, ShaderType::Fragment}
			}
		},
		m_SkyboxShader{
			{
				{ShaderFamily::SkyboxVertSrc, ShaderType::Vertex},
				{ShaderFamily::SkyboxFragSrc, ShaderType::Fragment}
			}
		},
		m_DirLightShader{
			{
				{ShaderFamily::LightPassVertSrc, ShaderType::Vertex},
				{ShaderFamily::DirLightPassFragSrc, ShaderType::Fragment}
			}
		},
		m_SpotLightShader{
			{
				{ShaderFamily::LightPassVertSrc, ShaderType::Vertex},
				{ShaderFamily::SpotLightPassFragSrc, ShaderType::Fragment}
			}
		},
		m_PointLightShader{
			{
				{ShaderFamily::LightPassVertSrc, ShaderType::Vertex},
				{ShaderFamily::PointLightPassFragSrc, ShaderType::Fragment}
			}
		}
	{
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);

		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
		glCullFace(GL_BACK);

		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

		glEnable(GL_STENCIL_TEST);
	}


	auto DeferredRenderer::Render() -> void
	{
		/* We don't render if there is no camera to use */
		if (Camera::Active() == nullptr)
		{
			return;
		}

		const auto& renderables{CollectRenderables()};

		const auto camViewMat{Camera::Active()->ViewMatrix()};
		const auto camProjMat{Camera::Active()->ProjectionMatrix()};

		const auto dirLight{DataManager::Instance().DirectionalLight()};
		const auto& spotLights{CollectSpotLights()};
		const auto& pointLights{CollectPointLights()};

		const auto dirShadow{dirLight != nullptr && dirLight->CastsShadow()};
		const auto spotShadows{
			std::accumulate(spotLights.begin(), spotLights.end(), 0ull, [](const auto sum, const auto elem)
			{
				if (elem->CastsShadow())
				{
					return sum + 1;
				}
				return sum;
			})
		};
		const auto pointShadows{
			std::accumulate(pointLights.begin(), pointLights.end(), 0ull, [](const auto sum, const auto elem)
			{
				if (elem->CastsShadow())
				{
					return sum + 1;
				}
				return sum;
			})
		};

		m_LightShader.Clear();
		m_LightShader["DIRLIGHT"] = std::to_string(dirLight != nullptr);
		m_LightShader["DIRLIGHT_SHADOW"] = std::to_string(dirShadow);
		m_LightShader["NUM_SPOTLIGHTS"] = std::to_string(spotLights.size());
		m_LightShader["NUM_SPOTLIGHT_SHADOWS"] = std::to_string(spotShadows);
		m_LightShader["NUM_POINTLIGHTS"] = std::to_string(pointLights.size());
		m_LightShader["NUM_POINTLIGHT_SHADOWS"] = std::to_string(pointShadows);
		m_LightShader["NUM_CASCADES"] = std::to_string(Settings::DirShadowCascadeCount());
		auto& lightShader{m_LightShader.GetPermutation()};

		auto& shadowShader{m_ShadowShader.GetPermutation()};
		auto& cubeShadowShader{m_CubeShadowShader.GetPermutation()};

		glStencilFunc(GL_ALWAYS, STENCIL_REF, STENCIL_AND_MASK);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		RenderGeometry(camViewMat, camProjMat, renderables);

		m_RenderBuffer.Clear();
		m_GBuffer.CopyStencilData(m_RenderBuffer.FramebufferName());

		glStencilFunc(GL_EQUAL, STENCIL_REF, STENCIL_AND_MASK);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

		shadowShader.Use();
		auto nextTexUnit{0};

		nextTexUnit = RenderDirShadowMap(dirLight, camViewMat.Inverse(), camProjMat, renderables, lightShader, shadowShader, nextTexUnit);
		nextTexUnit = RenderSpotShadowMaps(spotLights, renderables, lightShader, shadowShader, spotShadows, nextTexUnit);

		cubeShadowShader.Use();
		nextTexUnit = RenderPointShadowMaps(pointLights, renderables, lightShader, cubeShadowShader, pointShadows, nextTexUnit);

		nextTexUnit = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Position, nextTexUnit);
		nextTexUnit = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Normal, nextTexUnit);
		nextTexUnit = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Ambient, nextTexUnit);
		nextTexUnit = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Diffuse, nextTexUnit);
		nextTexUnit = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Specular, nextTexUnit);
		static_cast<void>(m_GBuffer.BindForReading(lightShader, GeometryBuffer::Texture::Shine, nextTexUnit));

		SetAmbientData(AmbientLight::Instance(), lightShader);
		SetDirectionalData(dirLight, lightShader);
		SetSpotData(spotLights, lightShader);
		SetPointData(pointLights, lightShader);

		lightShader.SetUniform("u_CamPos", Camera::Active()->Entity()->Transform()->Position());

		lightShader.Use();
		m_RenderBuffer.DrawQuad();

		glStencilFunc(GL_NOTEQUAL, STENCIL_REF, STENCIL_AND_MASK);
		RenderSkybox(camViewMat, camProjMat);

		m_RenderBuffer.CopyColorToDefaultBuffer();
	}


	auto DeferredRenderer::RenderGeometry(const Matrix4& camViewMat, const Matrix4& camProjMat, const std::vector<RenderableData>& renderables) -> void
	{
		m_GBuffer.Clear();
		m_GBuffer.BindForWriting();

		auto& shader{m_GeometryShader.GetPermutation()};
		shader.SetUniform("u_ViewProjMat", camViewMat * camProjMat);
		shader.Use();

		for (const auto& [renderable, instances, castsShadow] : renderables)
		{
			renderable->SetInstanceData(instances);
			renderable->DrawWithMaterial(shader, 0);
		}

		GeometryBuffer::UnbindFromWriting();
	}


	auto DeferredRenderer::RenderSkybox(const Matrix4& camViewMat, const Matrix4& camProjMat) -> void
	{
		if (const auto& background{Camera::Active()->Background()}; std::holds_alternative<Skybox>(background))
		{
			auto& skyboxShader{m_SkyboxShader.GetPermutation()};
			skyboxShader.SetUniform("u_ViewProjMat", static_cast<Matrix4>(static_cast<Matrix3>(camViewMat)) * camProjMat);
			skyboxShader.Use();

			m_RenderBuffer.BindAsRenderTarget();
			DataManager::Instance().CreateOrGetSkyboxImpl(std::get<Skybox>(background).AllFilePaths())->Draw(skyboxShader);
			RenderBuffer::UnbindAsRenderTarget();
		}
	}


	auto DeferredRenderer::RenderDirShadowMap(const DirectionalLight* dirLight,
	                                          const Matrix4& camViewInvMat,
	                                          const Matrix4& camProjMat,
	                                          const std::span<const RenderableData> renderables,
	                                          ShaderProgram& lightShader,
	                                          ShaderProgram& shadowShader,
	                                          const int nextTexUnit) -> int
	{
		if (!dirLight || !dirLight->CastsShadow())
		{
			return nextTexUnit;
		}

		static std::vector<Matrix4> cascadeMats;
		cascadeMats.clear();

		const auto lightViewMat{Matrix4::LookAt(Vector3{0}, dirLight->Direction(), Vector3::Up())};
		const auto cascadeBounds{m_DirShadowMap.CalculateCascadeBounds(*Camera::Active())};
		const auto numCascades{cascadeBounds.size()};

		for (std::size_t i = 0; i < numCascades; ++i)
		{
			const auto cascadeMat{m_DirShadowMap.CascadeMatrix(cascadeBounds[i], camViewInvMat, lightViewMat, dirLight->ShadowExtension())};
			cascadeMats.push_back(cascadeMat);

			shadowShader.SetUniform("u_WorldToClipMat", cascadeMat);

			m_DirShadowMap.BindForWriting(i);
			m_DirShadowMap.Clear();

			for (const auto& [renderable, instances, castsShadow] : renderables)
			{
				if (castsShadow)
				{
					renderable->SetInstanceData(instances);
					renderable->DrawWithoutMaterial();
				}
			}
		}

		CascadedShadowMap::UnbindFromWriting();

		lightShader.SetUniform("u_CascadeMatrices", cascadeMats);
		lightShader.SetUniform("u_CascadeBounds", CascadeFarBoundsClip(camProjMat, cascadeBounds));
		return m_DirShadowMap.BindForReading(lightShader, nextTexUnit);
	}


	auto DeferredRenderer::RenderSpotShadowMaps(const std::span<const SpotLight* const> spotLights,
	                                            const std::span<const RenderableData> renderables,
	                                            ShaderProgram& lightShader,
	                                            ShaderProgram& shadowShader,
	                                            const std::size_t numShadows,
	                                            int nextTexUnit) -> int
	{
		// Not really great in terms of allocations but will do for now
		while (m_SpotShadowMaps.size() < numShadows)
		{
			m_SpotShadowMaps.push_back(std::make_unique<SpotLightShadowMap>());
		}

		if (numShadows * 2 < m_SpotShadowMaps.size())
		{
			m_SpotShadowMaps.resize(numShadows);
		}

		for (auto i{0ull}, shadowInd{0ull}; i < spotLights.size(); ++i)
		{
			if (spotLights[i]->CastsShadow())
			{
				const auto lightWorldToClipMat
				{
					Matrix4::LookAt(
					                spotLights[i]->Entity()->Transform()->Position(),
					                spotLights[i]->Entity()->Transform()->Position() + spotLights[i]->Entity()->Transform()->Forward(),
					                Vector3::Up())
					*
					Matrix4::Perspective(math::ToRadians(spotLights[i]->OuterAngle() * 2),
					                     1.f,
					                     0.1f,
					                     spotLights[i]->Range())
				};

				shadowShader.SetUniform("u_WorldToClipMat", lightWorldToClipMat);

				m_SpotShadowMaps[shadowInd]->BindForWriting();
				m_SpotShadowMaps[shadowInd]->Clear();

				for (const auto& [renderable, instances, castsShadow] : renderables)
				{
					if (castsShadow)
					{
						renderable->SetInstanceData(instances);
						renderable->DrawWithoutMaterial();
					}
				}

				m_SpotShadowMaps[shadowInd]->UnbindFromWriting();

				lightShader.SetUniform("u_SpotShadowMats[" + std::to_string(shadowInd) + "]", lightWorldToClipMat);
				// This is also not great, this should somehow be done with BindForReading
				lightShader.SetUniform("u_SpotShadowMaps[" + std::to_string(shadowInd) + "]", nextTexUnit);
				nextTexUnit = m_SpotShadowMaps[shadowInd]->BindForReading(lightShader, nextTexUnit);
				++shadowInd;
			}
		}

		return nextTexUnit;
	}


	auto DeferredRenderer::RenderPointShadowMaps(const std::span<const PointLight* const> pointLights,
	                                             const std::span<const RenderableData> renderables,
	                                             ShaderProgram& lightShader,
	                                             ShaderProgram& shadowShader,
	                                             const std::size_t numShadows,
	                                             int nextTexUnit) -> int
	{
		// Not really great in terms of allocations but will do for now
		while (m_PointShadowMaps.size() < numShadows)
		{
			m_PointShadowMaps.push_back(std::make_unique<CubeShadowMap>());
		}

		if (numShadows * 2 < m_PointShadowMaps.size())
		{
			m_PointShadowMaps.resize(numShadows);
		}

		for (auto i{0ull}, shadowInd{0ull}; i < pointLights.size(); ++i)
		{
			if (pointLights[i]->CastsShadow())
			{
				std::array<Matrix4, 6> shadowViewProjMats;

				const auto shadowProj{
					Matrix4::Perspective(math::ToRadians(90),
					                     1,
					                     0.01f,
					                     pointLights[i]->Range())
				};

				static constexpr std::array cubeFaceMats
				{
					Matrix4{0, 0, 1, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1}, // +X
					Matrix4{0, 0, -1, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1}, // -X
					Matrix4{1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1}, // +Y
					Matrix4{1, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 1}, // -Y
					Matrix4{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, // +Z
					Matrix4{-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1} // -Z
				};

				std::ranges::transform(cubeFaceMats, shadowViewProjMats.begin(), [&](const auto& cubeFaceMat)
				{
					return Matrix4::Translate(-pointLights[i]->Entity()->Transform()->Position()) * cubeFaceMat * shadowProj;
				});

				shadowShader.SetUniform("u_ViewProjMats", shadowViewProjMats);
				shadowShader.SetUniform("u_LightPos", pointLights[i]->Entity()->Transform()->Position());
				shadowShader.SetUniform("u_FarPlane", pointLights[i]->Range());

				m_PointShadowMaps[shadowInd]->BindForWriting();
				m_PointShadowMaps[shadowInd]->Clear();

				for (const auto& [renderable, instances, castsShadow] : renderables)
				{
					if (castsShadow)
					{
						renderable->SetInstanceData(instances);
						renderable->DrawWithoutMaterial();
					}
				}

				m_PointShadowMaps[shadowInd]->UnbindFromWriting();

				// This is also not great, this should somehow be done with BindForReading
				for (auto j{0}; j < 6; ++j)
				{
					lightShader.SetUniform("u_PointShadowMaps[" + std::to_string(shadowInd) + "]", nextTexUnit + j);
				}
				nextTexUnit = m_PointShadowMaps[shadowInd]->BindForReading(lightShader, nextTexUnit);
				++shadowInd;
			}
		}

		return nextTexUnit;
	}


	auto DeferredRenderer::SetAmbientData(const AmbientLight& light, ShaderProgram& lightShader) -> void
	{
		lightShader.SetUniform("u_AmbientLight", light.Intensity());
	}


	auto DeferredRenderer::SetDirectionalData(const DirectionalLight* dirLight, ShaderProgram& lightShader) -> void
	{
		if (!dirLight)
		{
			return;
		}

		lightShader.SetUniform("u_DirLight.direction", dirLight->Direction());
		lightShader.SetUniform("u_DirLight.diffuseColor", dirLight->Diffuse());
		lightShader.SetUniform("u_DirLight.specularColor", dirLight->Specular());
	}


	auto DeferredRenderer::SetSpotData(const std::span<const SpotLight* const> spotLights, ShaderProgram& lightShader) -> void
	{
		constexpr auto shadowArrayName{"u_SpotLightsShadow["};
		constexpr auto noShadowArrayName{"u_SpotLightsNoShadow["};

		auto noShadowInd{0ull};
		auto shadowInd{0ull};

		for (const auto spotLight : spotLights)
		{
			const auto arrayPrefix{
				[&]() -> std::string
				{
					if (spotLight->CastsShadow())
					{
						auto ret{shadowArrayName + std::to_string(shadowInd) + "]."};
						++shadowInd;
						return ret;
					}
					auto ret{noShadowArrayName + std::to_string(noShadowInd) + "]."};
					++noShadowInd;
					return ret;
				}()
			};

			lightShader.SetUniform(arrayPrefix + "position", spotLight->Entity()->Transform()->Position());
			lightShader.SetUniform(arrayPrefix + "direction", spotLight->Entity()->Transform()->Forward());
			lightShader.SetUniform(arrayPrefix + "diffuseColor", spotLight->Diffuse());
			lightShader.SetUniform(arrayPrefix + "specularColor", spotLight->Specular());
			lightShader.SetUniform(arrayPrefix + "constant", spotLight->Constant());
			lightShader.SetUniform(arrayPrefix + "linear", spotLight->Linear());
			lightShader.SetUniform(arrayPrefix + "quadratic", spotLight->Quadratic());
			lightShader.SetUniform(arrayPrefix + "range", spotLight->Range());
			lightShader.SetUniform(arrayPrefix + "innerAngleCosine", math::Cos(math::ToRadians(spotLight->InnerAngle())));
			lightShader.SetUniform(arrayPrefix + "outerAngleCosine", math::Cos(math::ToRadians(spotLight->OuterAngle())));
		}
	}


	auto DeferredRenderer::SetPointData(const std::span<const PointLight* const> pointLights, ShaderProgram& lightShader) -> void
	{
		constexpr auto shadowArrayName{"u_PointLightsShadow["};
		constexpr auto noShadowArrayName{"u_PointLightsNoShadow["};

		auto noShadowInd{0ull};
		auto shadowInd{0ull};

		for (const auto pointLight : pointLights)
		{
			const auto arrayPrefix{
				[&]() -> std::string
				{
					if (pointLight->CastsShadow())
					{
						auto ret{shadowArrayName + std::to_string(shadowInd) + "]."};
						++shadowInd;
						return ret;
					}
					auto ret{noShadowArrayName + std::to_string(noShadowInd) + "]."};
					++noShadowInd;
					return ret;
				}()
			};

			lightShader.SetUniform(arrayPrefix + "position", pointLight->Entity()->Transform()->Position());
			lightShader.SetUniform(arrayPrefix + "diffuseColor", pointLight->Diffuse());
			lightShader.SetUniform(arrayPrefix + "specularColor", pointLight->Specular());
			lightShader.SetUniform(arrayPrefix + "constant", pointLight->Constant());
			lightShader.SetUniform(arrayPrefix + "linear", pointLight->Linear());
			lightShader.SetUniform(arrayPrefix + "quadratic", pointLight->Quadratic());
			lightShader.SetUniform(arrayPrefix + "range", pointLight->Range());
		}
	}
}
