#include "PointLight.hpp"

#include "../../instances/DataManager.hpp"

namespace leopph
{
	PointLight::PointLight(Object& owner) :
		AttenuatedLight{ owner }
	{
		impl::DataManager::RegisterPointLight(this);
	}

	PointLight::~PointLight()
	{
		impl::DataManager::UnregisterPointLight(this);
	}

}