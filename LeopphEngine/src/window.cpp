#include "window.h"
#include "camera.h"
#include "input.h"

#include <stdexcept>
#include <glad/glad.h>
#include <utility>
#include <map>


namespace leopph::implementation
{
	// init static members
	Window* Window::s_Instance{ nullptr };
	std::function<void(int, int)> Window::s_KeyCallback{};
	std::function<void(float, float)> Window::s_MouseCallback{};




	// set up a window and rendering context with callbacks
	Window& Window::Get(unsigned width, unsigned height, const std::string& title, bool fullscreen)
	{
		if (s_Instance == nullptr)
		{
			s_Instance = new Window(width, height, title, fullscreen);

			glfwMakeContextCurrent(s_Instance->m_Window);

			if (Camera::Active())
				Camera::Active()->AspectRatio(s_Instance->m_Width, s_Instance->m_Height);

			Input::RegisterCallbacks();

			glfwSetFramebufferSizeCallback(s_Instance->m_Window, FramebufferSizeCallback);
			glfwSetKeyCallback(s_Instance->m_Window, KeyCallbackManager);
			glfwSetCursorPosCallback(s_Instance->m_Window, MouseCallbackManager);

			glfwSetCursorPos(s_Instance->m_Window, 0, 0);
		}

		return *s_Instance;
	}





	// destroy instance
	void Window::Destroy()
	{
		delete s_Instance;
	}





	// constructor
	Window::Window(unsigned width, unsigned height, const std::string& title, bool fullscreen)
		: m_Width{ width }, m_Height{ height }, m_Fullscreen{ fullscreen }
	{
		if (!glfwInit())
			throw std::exception{};

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		GLFWmonitor* monitor{ nullptr };

		if (m_Fullscreen)
			monitor = glfwGetPrimaryMonitor();

		m_Window = glfwCreateWindow(m_Width, m_Height, title.data(), monitor, nullptr);
	}




	// destructor
	Window::~Window()
	{
		glfwDestroyWindow(m_Window);
	}





#pragma warning(push)
#pragma warning(disable: 4100)
	// framebuffer resize callback
	void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		glViewport(0, 0, width, height);

		s_Instance->m_Width = width;
		s_Instance->m_Height = height;

		if (Camera::Active() != nullptr)
			Camera::Active()->AspectRatio(s_Instance->m_Width, s_Instance->m_Height);
	}





	void Window::KeyCallbackManager(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (s_KeyCallback)
			s_KeyCallback(key, action);
	}
#pragma warning(pop)


	void Window::SetKeyCallback(std::function<void(int, int)> callback)
	{
		s_KeyCallback = std::move(callback);
	}






#pragma warning(push)
#pragma warning(disable: 4100)
	void Window::MouseCallbackManager(GLFWwindow* window, double x, double y)
	{
		if (s_MouseCallback)
			s_MouseCallback(static_cast<float>(x), static_cast<float>(y));
	}
#pragma warning(pop)

	void Window::SetMouseCallback(std::function<void(float, float)> callback)
	{
		s_MouseCallback = std::move(callback);
	}





	unsigned Window::Width() const
	{
		return m_Width;
	}

	void Window::Width(unsigned newWidth)
	{
		m_Width = newWidth;
		glfwSetWindowSize(m_Window, m_Width, m_Height);
	}

	unsigned Window::Height() const
	{
		return m_Height;
	}

	void Window::Height(unsigned newHeight)
	{
		m_Height = newHeight;
		glfwSetWindowSize(m_Window, m_Width, m_Height);
	}

	float Window::AspectRatio() const
	{
		return static_cast<float>(m_Width) / m_Height;
	}






	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	void Window::SwapBuffers()
	{
		glfwSwapBuffers(m_Window);
	}

	bool Window::ShouldClose()
	{
		return glfwWindowShouldClose(m_Window);
	}

	void Window::Clear()
	{
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}










	Window::CursorState Window::CursorMode()
	{
		static const std::map<decltype(GLFW_CURSOR_NORMAL), CursorState> cursorStates
		{
			{ GLFW_CURSOR_NORMAL, CursorState::Shown },
			{ GLFW_CURSOR_HIDDEN, CursorState::Hidden },
			{ GLFW_CURSOR_DISABLED, CursorState::Disabled }
		};

		return cursorStates.at(glfwGetInputMode(this->m_Window, GLFW_CURSOR));
	}

	void Window::CursorMode(CursorState newState)
	{
		static const std::map<CursorState, decltype(GLFW_CURSOR_NORMAL)> cursorStates
		{
			{ CursorState::Shown, GLFW_CURSOR_NORMAL },
			{ CursorState::Hidden, GLFW_CURSOR_HIDDEN },
			{ CursorState::Disabled, GLFW_CURSOR_DISABLED }
		};

		glfwSetInputMode(this->m_Window, GLFW_CURSOR, cursorStates.at(newState));
	}
}