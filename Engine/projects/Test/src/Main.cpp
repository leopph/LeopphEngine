#ifndef NDEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <Behavior.hpp>
#include <Input.hpp>
#include <Managed.hpp>
#include <Time.hpp>
#include <Window.hpp>
#include <RenderCore.hpp>


int main()
{
#ifndef NDEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	auto const window = leopph::Window::Create();

	if (!window)
	{
		return 1;
	}

	auto const renderer = leopph::RenderCore::Create(*window);

	if (!renderer)
	{
		return 2;
	}

	if (!leopph::init_input_system())
	{
		return 3;
	}

	if (!leopph::initialize_managed_runtime())
	{
		return 4;
	}

	leopph::init_time();

	while (!window->should_close())
	{
		window->process_events();

		if (!leopph::update_input_system())
		{
			return 5;
		}

		leopph::init_behaviors();
		leopph::tick_behaviors();
		leopph::tack_behaviors();

		if (!renderer->render())
		{
			return 6;
		}

		leopph::measure_time();
	}

	leopph::cleanup_managed_runtime();
	leopph::cleanup_input_system();
}
