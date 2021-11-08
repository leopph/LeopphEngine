#include "Rotate.hpp"

Rotate::Rotate(leopph::Entity& owner, const leopph::Vector3& axis, const float anglePerSec, const bool rotateLocally) :
	Behavior{ owner }, m_Axis{ axis }, m_Angle{ anglePerSec }, m_RotateLocally{ rotateLocally }
{}

void Rotate::OnFrameUpdate()
{
	const leopph::Quaternion rotation{ m_Axis, m_Angle * leopph::Time::DeltaTime() };

	if (m_RotateLocally)
	{
		Entity.Transform->RotateLocal(rotation);
	}
	else
	{
		Entity.Transform->RotateGlobal(rotation);
	}
}