#pragma once

#include "Component.hpp"
#include "../api/LeopphApi.hpp"
#include "../events/WindowEvent.hpp"
#include "../events/handling/EventReceiver.hpp"
#include "../math/Matrix.hpp"
#include "../misc/Color.hpp"
#include "../misc/Frustum.hpp"
#include "../rendering/Skybox.hpp"

#include <variant>


namespace leopph
{
	// Cameras are special Components that define the image that gets rendered.
	class Camera : public Component, public EventReceiver<internal::WindowEvent>
	{
		public:
			// Used for specifying parameters that affect the shape of the camera.
			enum class Side
			{
				Vertical,
				Horizontal
			};


			// The current camera that is used to render the scene.
			[[nodiscard]] LEOPPHAPI static
			auto Current() -> Camera*;

			// Set this Camera to be the current one.
			// The current Camera is used to render scene.
			// If no Camera instance exists, a newly created one will automatically be made current.
			// Only active Cameras can be made current.
			LEOPPHAPI
			auto MakeCurrent() -> void;

			// Set the near clip plane distance.
			// The near clip plane is the plane closest to the Camera, where rendering begins.
			// Objects closer to the Camera than this value will not be visible.
			LEOPPHAPI
			auto NearClipPlane(float newPlane) -> void;

			// Get the near clip plane distance.
			// The near clip plane is the plane closest to the Camera, where rendering begins.
			// Objects closer to the Camera than this value will not be visible.
			[[nodiscard]] LEOPPHAPI
			auto NearClipPlane() const -> float;

			// Set the far clip plane distance.
			// The far clip plane is the plane farthest from the Camera, where rendering ends.
			// Objects farther from the Camera than this value will not be visible.
			LEOPPHAPI
			auto FarClipPlane(float newPlane) -> void;

			// Get the far clip plane distance.
			// The far clip plane is the plane farthest from the Camera, where rendering ends.
			// Objects farther from the Camera than this value will not be visible.
			[[nodiscard]] LEOPPHAPI
			auto FarClipPlane() const -> float;

			// Get the Camera's background.
			// The Camera's background determines the visuals that the Camera "sees" where no Objects have been drawn to.
			[[nodiscard]] LEOPPHAPI
			auto Background() const -> std::variant<Color, Skybox> const&;

			// Set the Camera's background.
			// The Camera's background determines the visuals that the Camera "sees" where no Objects have been drawn to.
			LEOPPHAPI
			auto Background(std::variant<Color, Skybox> background) -> void;

			// Matrix that translates world positions to Camera-space.
			// Used during rendering.
			[[nodiscard]] LEOPPHAPI
			auto ViewMatrix() const -> Matrix4;

			// Matrix that projects Camera-space coordinates to Clip-space.
			// Used during rendering.
			[[nodiscard]] virtual
			auto ProjectionMatrix() const -> Matrix4 = 0;

			// The current frustum of the Camera in view space.
			// Used for internal calculations.
			[[nodiscard]] virtual
			auto Frustum() const -> Frustum = 0;

			// Deactivate the Camera.
			// If this was the current one, it will be set to nullptr.
			LEOPPHAPI
			auto Deactivate() -> void override;

			// Detach the Camera from its Entity.
			// If this was the current one, it will be set to nullptr.
			LEOPPHAPI
			auto Detach() -> void override;

			Camera(Camera const&) = delete;
			auto operator=(Camera const&) -> void = delete;

			Camera(Camera&&) = delete;
			auto operator=(Camera&&) -> void = delete;

			LEOPPHAPI ~Camera() override;

		protected:
			LEOPPHAPI Camera();

			[[nodiscard]]
			auto AspectRatio() const -> float;

		private:
			LEOPPHAPI
			auto OnEventReceived(EventParamType event) -> void override;

			static Camera* s_Current;

			float m_AspectRatio;
			float m_NearClip{0.1f};
			float m_FarClip{100.f};
			std::variant<Color, Skybox> m_Background;
	};
}
