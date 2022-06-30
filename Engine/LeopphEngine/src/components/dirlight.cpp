#include "DirLight.hpp"

#include "DataManager.hpp"
#include "InternalContext.hpp"
#include "Math.hpp"


namespace leopph
{
	auto DirectionalLight::Direction() const noexcept -> Vector3 const&
	{
		return Component::Owner()->Transform()->Forward();
	}


	auto DirectionalLight::ShadowExtension() const noexcept -> float
	{
		return m_ShadowRange;
	}


	auto DirectionalLight::ShadowExtension(float const newRange) -> void
	{
		m_ShadowRange = math::Clamp(newRange, 0, math::Abs(newRange));
	}


	auto DirectionalLight::Owner(Entity* entity) -> void
	{
		auto* const dataManager = internal::GetDataManager();

		if (InUse())
		{
			dataManager->UnregisterActiveDirLight(this);
		}

		Light::Owner(entity);

		if (InUse())
		{
			dataManager->RegisterActiveDirLight(this);
		}
	}


	auto DirectionalLight::Active(bool const active) -> void
	{
		auto* const dataManager = internal::GetDataManager();

		if (InUse())
		{
			dataManager->UnregisterActiveDirLight(this);
		}

		Light::Active(active);

		if (InUse())
		{
			dataManager->RegisterActiveDirLight(this);
		}
	}


	auto DirectionalLight::operator=(DirectionalLight const& other) -> DirectionalLight&
	{
		if (this == &other)
		{
			return *this;
		}

		auto* const dataManager = internal::GetDataManager();

		if (InUse())
		{
			dataManager->UnregisterActiveDirLight(this);
		}

		Light::operator=(other);
		m_ShadowRange = other.m_ShadowRange;

		if (InUse())
		{
			dataManager->RegisterActiveDirLight(this);
		}

		return *this;
	}


	auto DirectionalLight::Clone() const -> ComponentPtr<>
	{
		return CreateComponent<DirectionalLight>(*this);
	}


	DirectionalLight::~DirectionalLight()
	{
		internal::GetDataManager()->UnregisterActiveDirLight(this);
	}
}
