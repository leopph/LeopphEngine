#include "DeferredRenderer.hpp"

#include "../../components/Camera.hpp"
#include "../../components/lighting/AmbientLight.hpp"
#include "../../config/Settings.hpp"
#include "../../data/DataManager.hpp"
#include "../../math/LeopphMath.hpp"
#include "../../math/Matrix.hpp"
#include "../../windowing/WindowBase.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <iterator>



namespace leopph::impl
{
	DeferredRenderer::DeferredRenderer() :
		m_ShadowShader{
	{
		{ShaderFamily::ShadowMapVertSrc, ShaderType::Vertex}
	}
	},
		m_ShadowShaderInstanced{
			{
				{ShaderFamily::ShadowMapVertInstancedSrc, ShaderType::Vertex}
			}
	},
		m_CubeShadowShader{
			{
				{ShaderFamily::CubeShadowMapVertSrc, ShaderType::Vertex},
				{ShaderFamily::CubeShadowMapGeomSrc, ShaderType::Geometry},
				{ShaderFamily::CubeShadowMapFragSrc, ShaderType::Fragment}
			}
	},
		m_CubeShadowShaderInstanced{
	{
		{ShaderFamily::CubeShadowMapVertInstancedSrc, ShaderType::Vertex},
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
		m_GeometryShaderInstanced{
			{
				{ShaderFamily::GPassObjectVertInstancedSrc, ShaderType::Vertex},
				{ShaderFamily::GPassObjectFragSrc, ShaderType::Fragment}
			}
	},
		m_SkyboxShader{
			{
				{ShaderFamily::SkyboxVertSrc, ShaderType::Vertex},
				{ShaderFamily::SkyboxFragSrc, ShaderType::Fragment}
			}
	},
		m_AmbientShader{
			{
				{ShaderFamily::LightPassVertSrc, ShaderType::Vertex},
				{ShaderFamily::AmbLightFragSrc, ShaderType::Fragment}
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

		glDisable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);

		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
		glCullFace(GL_BACK);

		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	}


	void DeferredRenderer::Render()
	{
		/* We don't render if there is no camera to use */
		if (Camera::Active == nullptr)
		{
			return;
		}

		UpdateMatrices();

		const auto camViewMat{Camera::Active->ViewMatrix()};
		const auto camProjMat{Camera::Active->ProjectionMatrix()};

		const auto& pointLights{CollectPointLights()};
		const auto& spotLights{CollectSpotLights()};

		RenderGeometry(camViewMat, camProjMat);
		RenderGeometry(camViewMat, camProjMat);

		m_RenderTexture.Clear();

		RenderAmbientLight();
		glEnable(GL_BLEND);
		RenderDirectionalLights(camViewMat, camProjMat);
		RenderSpotLights(spotLights);
		RenderPointLights(pointLights);
		glDisable(GL_BLEND);

		RenderSkybox(camViewMat, camProjMat);

		m_RenderTexture.DrawToWindow();
	}


	void DeferredRenderer::RenderGeometry(const Matrix4& camViewMat, const Matrix4& camProjMat)
	{
		static auto nonInstFlagInfo{m_GeometryShader.GetFlagInfo()};
		static auto instFlagInfo{m_GeometryShaderInstanced.GetFlagInfo()};

		nonInstFlagInfo.Clear();
		instFlagInfo.Clear();

		auto& nonInstShader{m_GeometryShader.GetPermutation(nonInstFlagInfo)};
		auto& instShader{m_GeometryShaderInstanced.GetPermutation(instFlagInfo)};

		m_GBuffer.Clear();

		nonInstShader.SetUniform("u_ViewProjMat", camViewMat * camProjMat);
		instShader.SetUniform("u_ViewProjMat", camViewMat * camProjMat);

		m_GBuffer.BindForWriting();
		
		nonInstShader.Use();
		for (const auto& [renderable, component] : DataManager::NonInstancedRenderables())
		{
			const auto& [modelMat, normalMat]{DataManager::GetMatrices(component->Entity.Transform)};
			nonInstShader.SetUniform("u_ModelMat", modelMat);
			nonInstShader.SetUniform("u_NormalMat", normalMat);
			renderable->DrawShaded(nonInstShader, 0);
		}

		instShader.Use();
		for (const auto& [renderable, components] : DataManager::InstancedRenderables())
		{
			renderable.DrawShaded(instShader, 0);
		}

		m_GBuffer.UnbindFromWriting();
	}


	void DeferredRenderer::RenderAmbientLight()
	{
		static auto ambientFlagInfo{m_AmbientShader.GetFlagInfo()};
		auto& shader{m_AmbientShader.GetPermutation(ambientFlagInfo)};

		shader.SetUniform("u_AmbientLight", AmbientLight::Instance().Intensity());

		static_cast<void>(m_GBuffer.BindForReading(shader, GeometryBuffer::TextureType::Ambient, 0));
		shader.Use();

		glDisable(GL_DEPTH_TEST);
		m_RenderTexture.DrawToTexture();
		glEnable(GL_DEPTH_TEST);
	}


	void DeferredRenderer::RenderDirectionalLights(const Matrix4& camViewMat, const Matrix4& camProjMat)
	{
		const auto& dirLight{DataManager::DirectionalLight()};

		if (dirLight == nullptr)
		{
			return;
		}

		static auto lightFlagInfo{m_DirLightShader.GetFlagInfo()};
		static auto nonInstShadowFlagInfo{m_ShadowShader.GetFlagInfo()};
		static auto instShadowFlagInfo{m_ShadowShaderInstanced.GetFlagInfo()};

		lightFlagInfo.Clear();
		nonInstShadowFlagInfo.Clear();
		instShadowFlagInfo.Clear();

		lightFlagInfo["CAST_SHADOW"] = dirLight->CastsShadow();

		auto& lightShader{m_DirLightShader.GetPermutation(lightFlagInfo)};
		auto& nonInstShadowShader{m_ShadowShader.GetPermutation(nonInstShadowFlagInfo)};
		auto& instShadowShader{m_ShadowShaderInstanced.GetPermutation(instShadowFlagInfo)};

		auto texCount{0};

		texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Position, texCount);
		texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::Normal, texCount);
		texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Diffuse, texCount);
		texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Specular, texCount);
		texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Shine, texCount);

		lightShader.SetUniform("u_DirLight.direction", dirLight->Direction());
		lightShader.SetUniform("u_DirLight.diffuseColor", dirLight->Diffuse());
		lightShader.SetUniform("u_DirLight.specularColor", dirLight->Specular());
		lightShader.SetUniform("u_CameraPosition", Camera::Active->Entity.Transform->Position());

		if (dirLight->CastsShadow())
		{
			static std::vector<Matrix4> dirLightMatrices;
			static std::vector<float> cascadeFarBounds;

			dirLightMatrices.clear();
			cascadeFarBounds.clear();

			const auto cameraInverseMatrix{camViewMat.Inverse()};
			const auto lightViewMatrix{Matrix4::LookAt(dirLight->Range() * -dirLight->Direction(), Vector3{}, Vector3::Up())};
			const auto cascadeCount{Settings::DirectionalShadowCascadeCount()};

			for (std::size_t i = 0; i < cascadeCount; ++i)
			{
				const auto lightWorldToClip{m_DirShadowMap.WorldToClipMatrix(i, cameraInverseMatrix, lightViewMatrix)};
				dirLightMatrices.push_back(lightWorldToClip);

				nonInstShadowShader.SetUniform("u_WorldToClipMat", lightWorldToClip);
				instShadowShader.SetUniform("u_WorldToClipMat", lightWorldToClip);

				m_DirShadowMap.BindForWriting(i);
				m_DirShadowMap.Clear();

				nonInstShadowShader.Use();
				for (const auto& [renderable, component] : DataManager::NonInstancedRenderables())
				{
					nonInstShadowShader.SetUniform("u_ModelMat", DataManager::GetMatrices(component->Entity.Transform).first);
					renderable->DrawDepth();
				}

				instShadowShader.Use();
				for (const auto& [renderable, components] : DataManager::InstancedRenderables())
				{
					if (renderable.CastsShadow())
					{
						renderable.DrawDepth();
					}
				}
			}

			m_DirShadowMap.UnbindFromWriting();

			for (std::size_t i = 0; i < cascadeCount; ++i)
			{
				const auto viewSpaceBound{m_DirShadowMap.CascadeBoundsViewSpace(i)[1]};
				const Vector4 viewSpaceBoundVector{0, 0, viewSpaceBound, 1};
				const auto clipSpaceBoundVector{viewSpaceBoundVector * camProjMat};
				const auto clipSpaceBound{clipSpaceBoundVector[2]};
				cascadeFarBounds.push_back(clipSpaceBound);
			}

			lightShader.SetUniform("u_CascadeCount", static_cast<unsigned>(cascadeCount));
			lightShader.SetUniform("u_LightClipMatrices", dirLightMatrices);
			lightShader.SetUniform("u_CascadeFarBounds", cascadeFarBounds);
			static_cast<void>(m_DirShadowMap.BindForReading(lightShader, texCount));
		}

		lightShader.Use();
		glDisable(GL_DEPTH_TEST);
		m_RenderTexture.DrawToTexture();
		glEnable(GL_DEPTH_TEST);
	}


	void DeferredRenderer::RenderSpotLights(const std::vector<const SpotLight*>& spotLights)
	{
		if (spotLights.empty())
		{
			return;
		}

		static auto lightFlagInfo{m_DirLightShader.GetFlagInfo()};
		static auto nonInstShadowFlagInfo{m_ShadowShader.GetFlagInfo()};
		static auto instShadowFlagInfo{m_ShadowShaderInstanced.GetFlagInfo()};

		nonInstShadowFlagInfo.Clear();
		instShadowFlagInfo.Clear();

		auto& nonInstShadowShader{m_ShadowShader.GetPermutation(nonInstShadowFlagInfo)};
		auto& instShadowShader{m_ShadowShaderInstanced.GetPermutation(instShadowFlagInfo)};

		for (const auto& spotLight : spotLights)
		{
			lightFlagInfo.Clear();
			lightFlagInfo["CAST_SHADOW"] = spotLight->CastsShadow();
			auto& lightShader{m_SpotLightShader.GetPermutation(lightFlagInfo)};

			auto texCount{0};

			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Position, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Normal, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Diffuse, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Specular, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Shine, texCount);
			static_cast<void>(m_SpotShadowMap.BindForReading(lightShader, texCount));

			lightShader.SetUniform("u_CamPos", Camera::Active->Entity.Transform->Position());

			const auto lightWorldToClipMat
			{
				Matrix4::LookAt(spotLight->Entity.Transform->Position(), spotLight->Entity.Transform->Position() + spotLight->Entity.Transform->Forward(), Vector3::Up()) *
				Matrix4::Perspective(math::ToRadians(spotLight->OuterAngle() * 2), 1.f, 0.1f, spotLight->Range())
			};

			nonInstShadowShader.SetUniform("u_WorldToClipMat", lightWorldToClipMat);
			instShadowShader.SetUniform("u_WorldToClipMat", lightWorldToClipMat);

			m_SpotShadowMap.BindForWriting();
			m_SpotShadowMap.Clear();

			nonInstShadowShader.Use();
			for (const auto& [renderable, component] : DataManager::NonInstancedRenderables())
			{
				nonInstShadowShader.SetUniform("u_ModelMat", DataManager::GetMatrices(component->Entity.Transform).first);
				renderable->DrawDepth();
			}

			instShadowShader.Use();
			for (const auto& [renderable, components] : DataManager::InstancedRenderables())
			{
				if (renderable.CastsShadow())
				{
					renderable.DrawDepth();
				}
			}

			m_SpotShadowMap.UnbindFromWriting();

			lightShader.SetUniform("u_SpotLight.position", spotLight->Entity.Transform->Position());
			lightShader.SetUniform("u_SpotLight.direction", spotLight->Entity.Transform->Forward());
			lightShader.SetUniform("u_SpotLight.diffuseColor", spotLight->Diffuse());
			lightShader.SetUniform("u_SpotLight.specularColor", spotLight->Specular());
			lightShader.SetUniform("u_SpotLight.constant", spotLight->Constant());
			lightShader.SetUniform("u_SpotLight.linear", spotLight->Linear());
			lightShader.SetUniform("u_SpotLight.quadratic", spotLight->Quadratic());
			lightShader.SetUniform("u_SpotLight.range", spotLight->Range());
			lightShader.SetUniform("u_SpotLight.innerAngleCosine", math::Cos(math::ToRadians(spotLight->InnerAngle())));
			lightShader.SetUniform("u_SpotLight.outerAngleCosine", math::Cos(math::ToRadians(spotLight->OuterAngle())));
			lightShader.SetUniform("u_LightWorldToClipMatrix", lightWorldToClipMat);

			lightShader.Use();

			glDisable(GL_DEPTH_TEST);
			m_RenderTexture.DrawToTexture();
			glEnable(GL_DEPTH_TEST);
		}
	}


	void DeferredRenderer::RenderPointLights(const std::vector<const PointLight*>& pointLights)
	{
		if (pointLights.empty())
		{
			return;
		}

		static auto lightFlagInfo{m_PointLightShader.GetFlagInfo()};
		static auto nonInstShadowFlagInfo{m_ShadowShader.GetFlagInfo()};
		static auto instShadowFlagInfo{m_ShadowShaderInstanced.GetFlagInfo()};

		nonInstShadowFlagInfo.Clear();
		instShadowFlagInfo.Clear();

		auto& nonInstShadowShader{m_CubeShadowShader.GetPermutation(nonInstShadowFlagInfo)};
		auto& instShadowShader{m_CubeShadowShaderInstanced.GetPermutation(instShadowFlagInfo)};

		static std::vector<Matrix4> shadowViewProjMats;

		for (const auto& pointLight : pointLights)
		{
			lightFlagInfo.Clear();
			lightFlagInfo["CAST_SHADOW"] = pointLight->CastsShadow();
			auto& lightShader{m_PointLightShader.GetPermutation(lightFlagInfo)};

			auto texCount{0};
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Position, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Normal, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Diffuse, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Specular, texCount);
			texCount = m_GBuffer.BindForReading(lightShader, GeometryBuffer::TextureType::Shine, texCount);

			lightShader.SetUniform("u_PointLight.position", pointLight->Entity.Transform->Position());
			lightShader.SetUniform("u_PointLight.diffuseColor", pointLight->Diffuse());
			lightShader.SetUniform("u_PointLight.specularColor", pointLight->Specular());
			lightShader.SetUniform("u_PointLight.constant", pointLight->Constant());
			lightShader.SetUniform("u_PointLight.linear", pointLight->Linear());
			lightShader.SetUniform("u_PointLight.quadratic", pointLight->Quadratic());
			lightShader.SetUniform("u_PointLight.range", pointLight->Range());
			lightShader.SetUniform("u_CamPos", Camera::Active->Entity.Transform->Position());

			if (pointLight->CastsShadow())
			{
				static_cast<void>(m_PointShadowMap.BindForReading(lightShader, texCount));

				const auto shadowProj{Matrix4::Perspective(math::ToRadians(90), 1, 0.01f, pointLight->Range())};

				shadowViewProjMats.clear();

				static const std::array cubeFaceMats
				{
					 Matrix4{0, 0, 1, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1}, // +X
					 Matrix4{0, 0, -1, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1}, // -X
					 Matrix4{1, 0, 0, 0, 0, 0, 1, 0, 0, 1 ,0, 0, 0, 0, 0, 1}, // +Y
					 Matrix4{1, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 1}, // -Y
					 Matrix4{1, 0, 0, 0, 0, -1, 0, 0, 0 ,0, 1, 0, 0, 0, 0, 1}, // +Z
					 Matrix4{-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0 , 0, 0, 1} // -Z
				};

				std::ranges::transform(cubeFaceMats, std::back_inserter(shadowViewProjMats), [&](const auto& cubeFaceMat)
				{
					return Matrix4::Translate(-pointLight->Entity.Transform->Position()) * cubeFaceMat * shadowProj;
				});

				nonInstShadowShader.SetUniform("u_ViewProjMats", shadowViewProjMats);
				instShadowShader.SetUniform("u_ViewProjMats", shadowViewProjMats);
				nonInstShadowShader.SetUniform("u_LightPos", pointLight->Entity.Transform->Position());
				instShadowShader.SetUniform("u_LightPos", pointLight->Entity.Transform->Position());
				nonInstShadowShader.SetUniform("u_FarPlane", pointLight->Range());
				instShadowShader.SetUniform("u_FarPlane", pointLight->Range());

				m_PointShadowMap.BindForWriting();
				m_PointShadowMap.Clear();
				
				nonInstShadowShader.Use();
				for (const auto& [renderable, component] : DataManager::NonInstancedRenderables())
				{
					nonInstShadowShader.SetUniform("u_ModelMat", DataManager::GetMatrices(component->Entity.Transform).first);
					renderable->DrawDepth();
				}

				instShadowShader.Use();
				for (const auto& [renderable, components] : DataManager::InstancedRenderables())
				{
					if (renderable.CastsShadow())
					{
						renderable.DrawDepth();
					}
				}

				m_PointShadowMap.UnbindFromWriting();
			}

			lightShader.Use();

			glDisable(GL_DEPTH_TEST);
			m_RenderTexture.DrawToTexture();
			glEnable(GL_DEPTH_TEST);
		}
	}


	void DeferredRenderer::RenderSkybox(const Matrix4& camViewMat, const Matrix4& camProjMat)
	{
		if (const auto& skybox{Camera::Active->Background().skybox}; skybox.has_value())
		{
			static auto skyboxFlagInfo{m_SkyboxShader.GetFlagInfo()};
			auto& skyboxShader{m_SkyboxShader.GetPermutation(skyboxFlagInfo)};

			m_GBuffer.CopyDepthData(m_RenderTexture.FramebufferName());

			skyboxShader.SetUniform("u_ViewProjMat", static_cast<Matrix4>(static_cast<Matrix3>(camViewMat)) * camProjMat);

			m_RenderTexture.BindAsRenderTarget();

			skyboxShader.Use();

			DataManager::Skyboxes().find(skybox->AllFilePaths())->first.Draw(skyboxShader);

			m_RenderTexture.UnbindAsRenderTarget();
		}
	}
}
