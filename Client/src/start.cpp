#define LEOPPH_ENTRY

#include "Leopph.hpp"
#include "behaviors/CameraController.hpp"
#include "behaviors/FPSCounter.hpp"
#include "behaviors/Rotate.hpp"
#include "behaviors/ShadowSetter.hpp"
#include "behaviors/WindowTester.hpp"


auto leopph::Init() -> void
{
	Window::Title("LeopphEngine Demo");

	Input::CursorMode(CursorState::Disabled);

	const auto groupEntity = Entity::CreateEntity("group");
	groupEntity->Transform()->Rotate(Vector3::Up(), 180, Space::World);

	const auto playerEntity = Entity::CreateEntity("player");
	playerEntity->Transform()->Parent(groupEntity);

	const auto camera{playerEntity->CreateComponent<Camera>()};
	camera->Background(Skybox{"skybox/megasun/right.hdr","skybox/megasun/left.hdr","skybox/megasun/top.hdr","skybox/megasun/bottom.hdr","skybox/megasun/front.hdr","skybox/megasun/back.hdr"});

	camera->FarClipPlane(1000);

	playerEntity->CreateComponent<CameraController>();

	const auto portraitEntity = Entity::CreateEntity("portrait");
	portraitEntity->Transform()->Parent(groupEntity);
	portraitEntity->Transform()->Rotate(Vector3::Up(), 180);
	portraitEntity->Transform()->Translate(-5, 0, -10, Space::Local);
	const auto portrairModel = portraitEntity->CreateComponent<Model>("models/portrait/cropped_textured_mesh.obj");
	portrairModel->CastsShadow(true);

	const auto cubeEntity = Entity::CreateEntity("cube");
	cubeEntity->Transform()->Parent(groupEntity);
	cubeEntity->Transform()->LocalPosition(Vector3{0, 0, 5});
	const auto cubeModel = cubeEntity->CreateComponent<Model>("models/cube/cube.dae");
	cubeModel->CastsShadow(true);
	cubeEntity->CreateComponent<Rotate>(Vector3::Up(), 30.f);

	const auto dirLightEntity = Entity::CreateEntity("dirlight");
	dirLightEntity->Transform()->Parent(groupEntity);
	dirLightEntity->Transform()->Rotate(Vector3::Up(), 315, Space::World);
	dirLightEntity->Transform()->Rotate(Vector3::Right(), 45, Space::Local);
	const auto dirLight = dirLightEntity->CreateComponent<DirectionalLight>();
	dirLight->Diffuse(Vector3{0.5, 0.5, 0.5});
	dirLight->CastsShadow(true);

	Entity::CreateEntity("fpscounter")->CreateComponent<FPSCounter>();
	Entity::CreateEntity("windowstester")->CreateComponent<WindowTester>();

	const auto entity = Entity::CreateEntity();
	entity->Transform()->Parent(groupEntity);
	entity->Transform()->Translate(0, -10, 0);
	entity->Transform()->Rescale(-100, -100, -100);
	const auto model = entity->CreateComponent<Model>("models/snowy/scene.gltf");
	model->CastsShadow(true);

	Settings::DirShadowCascades(std::vector{ShadowCascade{4096, 20}, ShadowCascade{2048, 70}, ShadowCascade{1024, static_cast<std::size_t>(camera->FarClipPlane())}});

	Entity::CreateEntity()->CreateComponent<ShadowSetter>(std::vector{portrairModel, cubeModel, model});
}
