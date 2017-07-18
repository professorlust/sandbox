#include "index.hpp"
#include "workbench.hpp"
#include "imgui/imgui_internal.h"
#include "skeleton.hpp"

using namespace avl;

struct IKChain
{
    Pose root;
    Pose joint;
    Pose end;
};

tinygizmo::rigid_transform root_transform;
tinygizmo::rigid_transform joint_transform;
tinygizmo::rigid_transform end_transform;
tinygizmo::rigid_transform target_transform;

HumanSkeleton skeleton;

/*
// Make a cylinder with the bottom center at 0 and up on the +Y axis
inline Geometry make_cylinder(const float height, const float radiusTop, const float radiusBottom, const glm::vec4& colorTop, const glm::vec4& colorBottom, const int segments)
{
    vertex_positions.empty();
    vertex_colors.empty();
    nbSegments = segments;

    double angle = 0.0;
    vertex_positions.push_back(glm::vec4(0, height, 0, 1));
    vertex_colors.push_back(colorTop);
    for (unsigned int i = 0; i<nbSegments; ++i)
    {
        angle = ((double)i) / ((double)nbSegments)*2.0*3.14;
        vertex_positions.push_back(glm::vec4(radiusTop*std::cos(angle), height, radiusTop*std::sin(angle), 1.0));
        vertex_colors.push_back(colorTop);
        vertex_indexes.push_back(0);
        vertex_indexes.push_back((i + 1) % nbSegments + 1);
        vertex_indexes.push_back(i + 1);
    }

    vertex_positions.push_back(glm::vec4(0, 0, 0, 1));
    vertex_colors.push_back(colorBottom);
    for (unsigned int i = 0; i<nbSegments; ++i)
    {
        angle = ((double)i) / ((double)nbSegments)*2.0*3.14;
        vertex_positions.push_back(glm::vec4(radiusBottom*std::cos(angle), 0.0, radiusBottom*std::sin(angle), 1.0));
        vertex_colors.push_back(colorBottom);
        vertex_indexes.push_back(nbSegments + 1);
        vertex_indexes.push_back(nbSegments + 2 + (i + 1) % nbSegments);
        vertex_indexes.push_back(nbSegments + i + 2);
    }

    for (unsigned int i = 0; i<nbSegments; ++i)
    {
        vertex_indexes.push_back(i + 1);
        vertex_indexes.push_back((i + 1) % nbSegments + 1);
        vertex_indexes.push_back(nbSegments + 2 + (i + 1) % nbSegments);

        vertex_indexes.push_back(i + 1);
        vertex_indexes.push_back(nbSegments + 2 + (i + 1) % nbSegments);
        vertex_indexes.push_back(nbSegments + i + 2);
    }

}
*/

shader_workbench::shader_workbench() : GLFWApp(1200, 800, "Shader Workbench")
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    igm.reset(new gui::ImGuiManager(window));
    gui::make_dark_theme();

    normalDebug = shaderMonitor.watch("../assets/shaders/normal_debug_vert.glsl", "../assets/shaders/normal_debug_frag.glsl");

    sphere_mesh = make_sphere_mesh(0.1f);
    cylinder_mesh = make_mesh_from_geometry(make_tapered_capsule());

    make_sphere_mesh(0.25f); //make_cylinder_mesh(0.1, 0.2, 0.2, 32, 32, false);
    //cylinder_mesh.set_non_indexed(GL_LINES);

    gizmo.reset(new GlGizmo());

    root_transform.position.y = 1.f;
    joint_transform.position.y = 0.5f;
    joint_transform.position.z = -0.15f;
    end_transform.position.y = 0.f;

    target_transform.position = reinterpret_cast<const minalg::float3 &>(skeleton.bones[0].local_pose);

    cam.look_at({ 0, 9.5f, -6.0f }, { 0, 0.1f, 0 });
    flycam.set_camera(&cam);

    traverse_joint_chain(13, skeleton.bones);
}

shader_workbench::~shader_workbench()
{

}

void shader_workbench::on_window_resize(int2 size)
{

}

void shader_workbench::on_input(const InputEvent & event)
{
    igm->update_input(event);
    flycam.handle_input(event);

    if (event.type == InputEvent::KEY)
    {
        if (event.value[0] == GLFW_KEY_ESCAPE && event.action == GLFW_RELEASE) exit();
    }

    if (gizmo) gizmo->handle_input(event);
}

void shader_workbench::on_update(const UpdateEvent & e)
{
    flycam.update(e.timestep_ms);
    shaderMonitor.handle_recompile();
    elapsedTime += e.timestep_ms;
}

void shader_workbench::on_draw()
{
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    const float4x4 projectionMatrix = cam.get_projection_matrix(float(width) / float(height));
    const float4x4 viewMatrix = cam.get_view_matrix();
    const float4x4 viewProjectionMatrix = mul(projectionMatrix, viewMatrix);

    if (gizmo) gizmo->update(cam, float2(width, height));

    //tinygizmo::transform_gizmo("root", gizmo->gizmo_ctx, root_transform);
    //tinygizmo::transform_gizmo("joint", gizmo->gizmo_ctx, joint_transform);
    //tinygizmo::transform_gizmo("end", gizmo->gizmo_ctx, end_transform);
    tinygizmo::transform_gizmo("target", gizmo->gizmo_ctx, target_transform);

    const float3 rootPosition = reinterpret_cast<const float3 &>(root_transform.position);
    const float3 jointPosition = reinterpret_cast<const float3 &>(joint_transform.position);
    const float3 endPosition = reinterpret_cast<const float3 &>(end_transform.position);

    const float3 jointTarget = float3(0, 0, 0);
    const float3 effectorPosition = reinterpret_cast<const float3 &>(target_transform.position);

    float3 outJointPosition;
    float3 outEndPosition;

    SolveTwoBoneIK(rootPosition, jointPosition, endPosition, jointTarget, effectorPosition, outJointPosition, outEndPosition, false, 1.f, 1.f);

    float4x4 rootMatrix = reinterpret_cast<const float4x4 &>(root_transform.matrix());
    float4x4 jointMatrix = reinterpret_cast<const float4x4 &>(joint_transform.matrix());
    float4x4 endMatrix = reinterpret_cast<const float4x4 &>(end_transform.matrix());

    float4x4 outJointMatrix = mul(make_translation_matrix(outJointPosition), make_scaling_matrix(0.5));
    float4x4 outEffectorMatrix = mul(make_translation_matrix(outEndPosition), make_scaling_matrix(0.5));

    gpuTimer.start();

    // Main Scene
    {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);

        glViewport(0, 0, width, height);
        glClearColor(1.f, 1.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        normalDebug->bind();
        normalDebug->uniform("u_viewProj", viewProjectionMatrix);

        // Some debug models
        /*
        for (const auto & model : { rootMatrix, jointMatrix, endMatrix, outJointMatrix, outEffectorMatrix })
        {
            normalDebug->uniform("u_modelMatrix", model);
            normalDebug->uniform("u_modelMatrixIT", inv(transpose(model)));
            sphere_mesh.draw_elements();
        }
        */

        skeleton.bones[0].local_pose = reinterpret_cast<const float4x4 &>(target_transform.matrix());

        auto skeletonBones = compute_static_pose(skeleton.bones);

        for (const auto & bone : skeletonBones)
        {
            normalDebug->uniform("u_modelMatrix", bone);
            normalDebug->uniform("u_modelMatrixIT", inv(transpose(bone)));
            cylinder_mesh.draw_elements();
        }

        normalDebug->unbind();
    }

    gpuTimer.stop();

    igm->begin_frame();
    // ... 
    igm->end_frame();

    if (gizmo) gizmo->draw();

    gl_check_error(__FILE__, __LINE__);

    glfwSwapBuffers(window);
}

IMPLEMENT_MAIN(int argc, char * argv[])
{
    try
    {
        shader_workbench app;
        app.main_loop();
    }
    catch (const std::exception & e)
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
