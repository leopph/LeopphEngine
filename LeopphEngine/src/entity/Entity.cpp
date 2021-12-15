#include "Entity.hpp"

#include "../data/DataManager.hpp"
#include "../util/logger.h"

#include <iterator>
#include <limits>
#include <stdexcept>


namespace leopph
{
	Entity* Entity::Find(const std::string& name)
	{
		return impl::DataManager::Find(name);
	}


	Entity::Entity(std::string name) :
		Name{m_Name},
		Transform{m_Transform},
		m_Name{name.empty() ? "Entity" + std::to_string(impl::DataManager::EntitiesAndComponents().size()) : std::move(name)}
	{
		if (Find(this->Name) != nullptr)
		{
			std::string newName{};
			for (std::size_t i = 0; i < std::numeric_limits<std::size_t>::max(); i++)
			{
				newName = m_Name + "(" + std::to_string(i) + ")";
				if (Find(newName) == nullptr)
				{
					break;
				}
			}
			if (newName.empty())
			{
				const auto errMsg{"Could not solve name conflict during creation of new Entity [" + m_Name + "]."};
				impl::Logger::Instance().Critical(errMsg);
				throw std::invalid_argument{errMsg};
			}
			impl::Logger::Instance().Warning("Entity name [" + m_Name + "] is already taken. Renaming Entity to [" + newName + "]...");
			m_Name = newName;
		}

		impl::DataManager::Register(this);
		m_Transform = CreateComponent<leopph::Transform>();
	}


	Entity::Entity() :
		Entity{std::string{}}
	{}


	Entity::~Entity() noexcept
	{
		impl::DataManager::Unregister(this);
	}


	std::vector<Component*> Entity::Components() const
	{
		const auto& components{impl::DataManager::ComponentsOfEntity(this)};
		std::vector<Component*> ret(components.size());
		std::ranges::transform(components, std::back_inserter(ret), [](const auto& compPtr)
		{
			return compPtr.get();
		});
		return ret;
	}


	void Entity::RemoveComponent(const Component* component) const
	{
		if (&component->Entity != this)
		{
			const auto msg{"Error while trying to remove Component at address [" + std::to_string(reinterpret_cast<unsigned long long>(component)) + "] from Entity [" + m_Name + "]. The Component's owning Entity is different from this."};
			impl::Logger::Instance().Error(msg);
		}
		else
		{
			impl::DataManager::UnregisterComponentFromEntity(this, component);
		}
	}

	void Entity::RegisterComponent(std::unique_ptr<Component>&& component) const
	{
		impl::DataManager::RegisterComponentForEntity(this, std::move(component));
	}
}
