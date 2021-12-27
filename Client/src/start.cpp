#define LEOPPH_ENTRY

#include "Leopph.hpp"
#include "behaviors/CameraController.hpp"
#include "behaviors/FPSCounter.hpp"
#include "behaviors/Rotate.hpp"
#include "behaviors/WindowTester.hpp"


void leopph::Init()
{
	Window::Title("LeopphEngine Demo");

	Input::CursorMode(CursorState::Disabled);

	const auto groupEntity = Entity::CreateEntity("group");
	groupEntity->Transform()->Rotate(Vector3::Up(), 180, Space::World);

	const auto playerEntity = Entity::CreateEntity("player");
	playerEntity->Transform()->Parent(groupEntity);

	const auto camera{playerEntity->CreateComponent<Camera>()};
	camera->Background(CameraBackground
		{
			.skybox{
				Skybox
				{
					"skybox/megasun/right.hdr",
					"skybox/megasun/left.hdr",
					"skybox/megasun/top.hdr",
					"skybox/megasun/bottom.hdr",
					"skybox/megasun/front.hdr",
					"skybox/megasun/back.hdr"
				}
			}
		});

	playerEntity->CreateComponent<CameraController>();

	const auto portraitEntity = Entity::CreateEntity("portrait");
	portraitEntity->Transform()->Parent(groupEntity);
	portraitEntity->Transform()->Rotate(Vector3::Up(), 180);
	portraitEntity->Transform()->LocalPosition(Vector3{0, 0, 5});
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

	const auto spotLightEntity = Entity::CreateEntity("spotlight");
	spotLightEntity->Transform()->Parent(groupEntity);
	const auto spotLight = spotLightEntity->CreateComponent<SpotLight>();
	spotLight->InnerAngle(45);
	spotLight->OuterAngle(60);
	spotLight->CastsShadow(true);

	const auto pointLightEntity = Entity::CreateEntity("pointlight");
	pointLightEntity->Transform()->Parent(groupEntity);
	pointLightEntity->Transform()->LocalPosition(Vector3{0, 0, 3.5});
	pointLightEntity->Transform()->LocalScale(Vector3{0.1, 0.1, 0.1});
	const auto pointLight = pointLightEntity->CreateComponent<PointLight>();
	pointLight->Range(15);
	pointLight->CastsShadow(true);


	AmbientLight::Instance().Intensity(Vector3{0, 0, 0});

	(Entity::CreateEntity())->CreateComponent<FPSCounter>();
	(Entity::CreateEntity())->CreateComponent<WindowTester>();
}
