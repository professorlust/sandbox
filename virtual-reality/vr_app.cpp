#include "vr_app.hpp"

VirtualRealityApp::VirtualRealityApp() : GLFWApp(1280, 800, "VR")
{
	int windowWidth, windowHeight;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);

	cameraController.set_camera(&debugCam);

	try
	{
		hmd.reset(new OpenVR_HMD());

		const uint2 targetSize = hmd->get_recommended_render_target_size();
		renderer.reset(new VR_Renderer({ (float)targetSize.x, (float)targetSize.y }));

		glfwSwapInterval(0);
	}
	catch (const std::exception & e)
	{
		std::cout << "OpenVR Exception: " << e.what() << std::endl;
		renderer.reset(new VR_Renderer({ (float)windowWidth * 0.5f, (float)windowHeight })); // per eye resolution
	}

	setup_physics();

	setup_scene();

	gl_check_error(__FILE__, __LINE__);
}

VirtualRealityApp::~VirtualRealityApp()
{
	hmd.reset();
}

void VirtualRealityApp::setup_physics()
{
	physicsEngine.reset(new BulletEngineVR());

	physicsDebugRenderer.reset(new PhysicsDebugRenderer()); // Sets up a few gl objects
	physicsDebugRenderer->setDebugMode(
		btIDebugDraw::DBG_DrawWireframe |
		btIDebugDraw::DBG_DrawContactPoints |
		btIDebugDraw::DBG_DrawConstraints |
		btIDebugDraw::DBG_DrawConstraintLimits);

	// Hook up debug renderer
	physicsEngine->get_world()->setDebugDrawer(physicsDebugRenderer.get());
}

void VirtualRealityApp::setup_scene()
{
	// Materials first since other objects need to reference them
	auto normalShader = shaderMonitor.watch("../assets/shaders/normal_debug_vert.glsl", "../assets/shaders/normal_debug_frag.glsl");
	scene.namedMaterialList["material-debug"] = std::make_shared<DebugMaterial>(normalShader);

	scene.grid.set_origin(float3(0, -.01f, 0));

	btCollisionShape * ground = new btStaticPlaneShape({ 0, 1, 0 }, 0);
	auto groundObject = std::make_shared<BulletObjectVR>(new btDefaultMotionState(), ground, physicsEngine->get_world());
	physicsEngine->add_object(groundObject.get());
	scene.physicsObjects.push_back(groundObject);

	StaticMesh cube;
	cube.set_static_mesh(make_cube(), 0.25f);
	cube.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 0, 0)));
	cube.set_material(scene.namedMaterialList["material-debug"].get());

	btCollisionShape * cubeCollisionShape = new btBoxShape(to_bt(cube.get_bounds().size() * 0.5f));
	auto cubePhysicsObj = std::make_shared<BulletObjectVR>(new btDefaultMotionState(), cubeCollisionShape, physicsEngine->get_world());
	cube.set_physics_component(cubePhysicsObj.get());

	physicsEngine->add_object(cubePhysicsObj.get());
	scene.physicsObjects.push_back(cubePhysicsObj);
	scene.models.push_back(std::move(cube));

	if (hmd)
	{
		auto controllerRenderModel = hmd->get_controller_render_data();

		scene.leftController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_LeftHand), controllerRenderModel));
		scene.rightController.reset(new MotionControllerVR(physicsEngine, hmd->get_controller(vr::TrackedControllerRole_RightHand), controllerRenderModel));

		// This section sucks I think:
		auto texturedShader = shaderMonitor.watch("../assets/shaders/textured_model_vert.glsl", "../assets/shaders/textured_model_frag.glsl");
		auto texturedMaterial = std::make_shared<TexturedMaterial>(texturedShader);
		texturedMaterial->set_diffuse_texture(controllerRenderModel->tex);
		scene.namedMaterialList["material-textured"] = texturedMaterial;

		// Create renderable controllers
		for (int i = 0; i < 2; ++i)
		{
			StaticMesh controller;
			controller.set_static_mesh(controllerRenderModel->mesh, 1.0f);
			controller.set_pose(Pose(float4(0, 0, 0, 1), float3(0, 0, 0)));
			controller.set_material(scene.namedMaterialList["material-textured"].get());
			scene.controllers.push_back(std::move(controller));
		}
	}
}

void VirtualRealityApp::on_window_resize(int2 size)
{

}

void VirtualRealityApp::on_input(const InputEvent & event) 
{
	cameraController.handle_input(event);
}

void VirtualRealityApp::on_update(const UpdateEvent & e) 
{
	cameraController.update(e.timestep_ms);

	shaderMonitor.handle_recompile();

	if (hmd)
	{
		scene.leftController->update_controller_pose(hmd->get_controller(vr::TrackedControllerRole_LeftHand).p);
		scene.rightController->update_controller_pose(hmd->get_controller(vr::TrackedControllerRole_RightHand).p);

		physicsEngine->update();

		btTransform leftTranslation;
		scene.leftController->physicsObject->body->getMotionState()->getWorldTransform(leftTranslation);

		btTransform rightTranslation;
		scene.rightController->physicsObject->body->getMotionState()->getWorldTransform(rightTranslation);

		// Workaround until a nicer system is in place
		for (auto & obj : scene.physicsObjects)
		{
			for (auto & model : scene.models)
			{
				if (model.get_physics_component() == obj.get())
				{
					btTransform trans;
					obj->body->getMotionState()->getWorldTransform(trans);
					model.set_pose(make_pose(trans));
				}
			}
		}

		// Update the the pose of the controller mesh we render
		scene.controllers[0].set_pose(hmd->get_controller(vr::TrackedControllerRole_LeftHand).p);
		scene.controllers[1].set_pose(hmd->get_controller(vr::TrackedControllerRole_RightHand).p);
	}

	// Iterate scene and make objects visible to the renderer
	auto renderableObjectsInScene = scene.gather();
	for (auto & obj : renderableObjectsInScene) { renderer->add_renderable(obj); }
	renderer->add_debug_renderable(&scene.grid);
}

void VirtualRealityApp::on_draw()
{
	glfwMakeContextCurrent(window);

	int width, height;
	glfwGetWindowSize(window, &width, &height);
	glViewport(0, 0, width, height);

	physicsEngine->get_world()->debugDrawWorld();
	renderer->add_debug_renderable(physicsDebugRenderer.get());

	if (hmd)
	{
		EyeData left = { hmd->get_eye_pose(vr::Hmd_Eye::Eye_Left), hmd->get_proj_matrix(vr::Hmd_Eye::Eye_Left, 0.01, 25.f) };
		EyeData right = { hmd->get_eye_pose(vr::Hmd_Eye::Eye_Right), hmd->get_proj_matrix(vr::Hmd_Eye::Eye_Right, 0.01, 25.f) };
		renderer->set_eye_data(left, right);
		renderer->render_frame();
		hmd->submit(renderer->get_eye_texture(Eye::LeftEye), renderer->get_eye_texture(Eye::RightEye));
		hmd->update();
	}
	else
	{
		const float4x4 projMatrix = debugCam.get_projection_matrix(float(width) / float(height));
		const EyeData centerEye = { debugCam.get_pose(), projMatrix };
		renderer->set_eye_data(centerEye, centerEye);
		renderer->render_frame();
	}

	Bounds2D rect{ { 0.f, 0.f },{ (float)width,(float)height } };

	const float mid = (rect.min().x + rect.max().x) / 2.f;
	ScreenViewport leftviewport = { rect.min(),{ mid - 2.f, rect.max().y }, renderer->get_eye_texture(Eye::LeftEye) };
	ScreenViewport rightViewport = { { mid + 2.f, rect.min().y }, rect.max(), renderer->get_eye_texture(Eye::RightEye) };

	viewports.clear();
	viewports.push_back(leftviewport);
	viewports.push_back(rightViewport);

	if (viewports.size())
	{
		glUseProgram(0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	for (auto & v : viewports)
	{
		glViewport(v.bmin.x, height - v.bmax.y, v.bmax.x - v.bmin.x, v.bmax.y - v.bmin.y);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, v.texture);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(-1, -1);
		glTexCoord2f(1, 0); glVertex2f(+1, -1);
		glTexCoord2f(1, 1); glVertex2f(+1, +1);
		glTexCoord2f(0, 1); glVertex2f(-1, +1);
		glEnd();
		glDisable(GL_TEXTURE_2D);
	}

	physicsDebugRenderer->clear();

	glfwSwapBuffers(window);
	gl_check_error(__FILE__, __LINE__);

}


int main(int argc, char * argv[])
{
	VirtualRealityApp app;
	app.main_loop();
	return EXIT_SUCCESS;
}