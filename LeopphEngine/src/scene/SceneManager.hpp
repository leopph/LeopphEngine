#pragma once

#include "Scene.hpp"
#include "../api/LeopphApi.hpp"

#include <memory>
#include <string_view>
#include <vector>


namespace leopph
{
	class SceneManager
	{
		public:
			[[nodiscard]] LEOPPHAPI static auto Instance() -> SceneManager&;

			auto AddScene(std::unique_ptr<Scene> scene) -> void;

			// Deletes the specified scene instance.
			// Nullptr is silently ignored.
			// If it is the last scene instance, it will not be deleted.
			// Returns whether deletion took place.
			// If the active scene is deleted, the new active scene will be the one with the lowest id.
			LEOPPHAPI auto DeleteScene(const Scene* scene) -> bool;

			// Deletes the scene with the specified id.
			// Unused ids are silently ignored.
			// If there is only one scene instance, it will not be deleted.
			// Returns whether deletion took place.
			// If the active scene is deleted, the new active scene will be the one with the lowest id.
			LEOPPHAPI auto DeleteScene(std::size_t id) -> bool;

			// Deletes the scene that was created with the specified name.
			// Unused names are silently ignored.
			// If there is only one scene instance, it will not be deleted.
			// Returns whether deletion took place.
			// If the active scene is deleted, the new active scene will be the one with the lowest id.
			LEOPPHAPI auto DeleteScene(std::string_view name) -> bool;

			// Returns a pointer to the scene with the specified id,
			// Or nullptr if not found.
			[[nodiscard]] LEOPPHAPI auto FindScene(std::size_t id) const -> Scene*;

			// Returns a pointer to the scene that was created with the specified name,
			// Or nullptr if not found.
			[[nodiscard]] LEOPPHAPI auto FindScene(std::string_view name) const -> Scene*;

			// Returns the currently active scene.
			// There is always one active scene.
			[[nodiscard]] LEOPPHAPI auto CurrentScene() const -> Scene&;

			SceneManager(const SceneManager& other) = delete;
			auto operator=(const SceneManager& other) -> SceneManager& = delete;

			SceneManager(SceneManager&& other) noexcept = delete;
			auto operator=(SceneManager&& other) noexcept -> SceneManager& = delete;

		private:
			SceneManager() = default;
			~SceneManager() = default;

			// Returns an iterator to the first scene that's id is not less than the passed id.
			// Returns end iterator if there is none.
			[[nodiscard]] constexpr static auto GetSceneIterator(auto self, std::size_t id);

			// Sorts the scenes in ascending order based on id.
			auto SortScenes() -> void;

			Scene* m_Current{new Scene{0}};
			std::vector<std::unique_ptr<Scene>> m_Scenes{
				[this]
				{
					std::vector<std::unique_ptr<Scene>> scenes;
					scenes.emplace_back(m_Current);
					return scenes;
				}()
			};
	};


	constexpr auto SceneManager::GetSceneIterator(auto self, const std::size_t id)
	{
		return std::lower_bound(self->m_Scenes.begin(), self->m_Scenes.end(), id, [](const std::unique_ptr<Scene>& scene, const std::size_t targetId)
		{
			return scene->Id() < targetId;
		});
	}
}
