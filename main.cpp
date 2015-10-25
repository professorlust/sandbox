#include <iostream>
#include <sstream>

#include "util.hpp"
#include "string_utils.hpp"
#include "geometric.hpp"
#include "linear_algebra.hpp"
#include "math_util.hpp"
#include "circular_buffer.hpp"
#include "concurrent_queue.hpp"
#include "try_locker.hpp"
#include "running_statistics.hpp"
#include "time_keeper.hpp"
#include "human_time.hpp"
#include "signal.hpp" // todo: rename
#include "one_euro.hpp"
#include "json.hpp"
#include "geometry.hpp"
#include "pid_controller.hpp" // todo: do integration in pid class
#include "base64.hpp"
#include "dsp_filters.hpp"
#include "bit_mask.hpp"
#include "file_io.hpp"
#include "GlMesh.hpp"
#include "GlShader.hpp"
#include "GlTexture.hpp"
#include "universal_widget.hpp"
#include "arcball.hpp"
#include "sketch.hpp"
#include "glfw_app.hpp"
#include "tinyply.h"
#include "renderable_grid.hpp"
#include "hosek.hpp"
#include "nvg.hpp"

using namespace math;
using namespace util;
using namespace tinyply;
using namespace gfx;

GlMesh make_sphere_mesh(float radius)
{
    Geometry sphereGeom;
    
    int U = 16, V = 16;
    
    for (int ui = 0; ui < U; ++ui)
    {
        for (int vi = 0; vi < V; ++vi)
        {
            float u = float(ui) / (U - 1) * ANVIL_PI;
            float v = float(vi) / (V - 1) * 2 * ANVIL_PI;
            float3 normal = spherical(u, v);
            sphereGeom.vertices.push_back({normal * radius});
            sphereGeom.normals.push_back(normal);
        }
    }
    
    for (uint32_t ui = 0; ui < U; ++ui)
    {
        uint32_t un = (ui + 1) % U;
        for (uint32_t vi = 0; vi < V; ++vi)
        {
            uint32_t vn = (vi + 1) % V;
            sphereGeom.faces.push_back({ui * V + vi, un * V + vi, un * V + vn});
            sphereGeom.faces.push_back({ui * V + vi, un * V + vn, ui * V + vn});
        }
    }
    
    return make_mesh_from_geometry(sphereGeom);
}

struct ExperimentalApp : public GLFWApp
{
    
    Model sofaModel;
    Geometry sofaGeometry;
    
    GlTexture emptyTex;
    
    std::unique_ptr<GLTextureView> myTexture;
    std::unique_ptr<GlShader> simpleShader;
    
    UWidget rootWidget;
    
    GlCamera camera;
    Sphere cameraSphere;
    //Arcball myArcball;
    
    float2 lastCursor;
    bool isDragging = false;
    
    RenderableGrid grid;
    
    FPSCameraController cameraController;
    
    float sunTheta = 60;
    float sunPhi = 200;
    float skyTurbidity = 6;
    
    HosekSky sky = HosekSky::compute(to_radians(sunTheta), skyTurbidity, 1.33f);
    
    GlMesh skyMesh;
    
    std::unique_ptr<GlShader> hosek_sky;
    
    ExperimentalApp() : GLFWApp(600, 600, "Experimental App")
    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        try
        {
            std::ifstream ss("assets/sofa.ply", std::ios::binary);
            PlyFile file(ss);
            
            std::vector<float> verts;
            std::vector<int32_t> faces;
            std::vector<float> texCoords;
            
            uint32_t vertexCount = file.request_properties_from_element("vertex", {"x", "y", "z"}, verts);
            uint32_t numTriangles = file.request_properties_from_element("face", {"vertex_indices"}, faces, 3);
            uint32_t uvCount = file.request_properties_from_element("face", {"texcoord"}, texCoords, 6);
            
            file.read(ss);
            
            sofaGeometry.vertices.reserve(vertexCount);
            for (int i = 0; i < vertexCount * 3; i+=3)
                sofaGeometry.vertices.push_back(math::float3(verts[i], verts[i+1], verts[i+2]));
            
            sofaGeometry.faces.reserve(numTriangles);
            for (int i = 0; i < numTriangles * 3; i+=3)
                sofaGeometry.faces.push_back(math::uint3(faces[i], faces[i+1], faces[i+2]));
            
            sofaGeometry.texCoords.reserve(uvCount);
            for (int i = 0; i < uvCount * 6; i+= 2)
                sofaGeometry.texCoords.push_back(math::float2(texCoords[i], texCoords[i+1]));

            sofaGeometry.compute_normals();
            sofaGeometry.compute_bounds();
            sofaGeometry.compute_tangents();
            
            std::cout << "Read " << vertexCount << " vertices..." << std::endl;
            
        }
        catch (std::exception e)
        {
            std::cerr << "Caught exception: " << e.what() << std::endl;
        }
        
        sofaModel.mesh = make_mesh_from_geometry(sofaGeometry);
        sofaModel.bounds = sofaGeometry.compute_bounds();
        
        gfx::gl_check_error(__FILE__, __LINE__);
        
        simpleShader.reset(new gfx::GlShader(read_file_text("assets/simple.vert"), read_file_text("assets/simple.frag")));
        
        //std::vector<uint8_t> whitePixel = {255,255,255,255};
        //emptyTex.load_data(1, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel.data());
        
        emptyTex = load_image("assets/anvil.png");
        
        rootWidget.bounds = {0, 0, (float) width, (float) height};
        rootWidget.add_child( {{0,+5},{0,+5},{0.5,0},{0.5,0}}, std::make_shared<UWidget>());
        //rootWidget.add_child( {{0, 0}, {0.5, +10}, {0.5, 0}, {1.0, -10}}, std::make_shared<UWidget>());
        
        rootWidget.layout();
    
        myTexture.reset(new GLTextureView(emptyTex.get_gl_handle()));
        
        cameraController.set_camera(&camera);
        camera.fov = 65;
        
        //skyMesh = std::move(make_sphere_mesh(0.1, 12, 12));
        skyMesh = make_sphere_mesh(1.0f);
        hosek_sky.reset(new gfx::GlShader(read_file_text("procedural_sky/sky_vert.glsl"), read_file_text("procedural_sky/sky_hosek_frag.glsl")));
        
        //cameraSphere = Sphere(sofaModel.bounds.center(), 1);
        //myArcball = Arcball(&camera, cameraSphere);
        //myArcball.set_constraint_axis(float3(0, 1, 0));
        
    }
    
    void on_input(const InputEvent & event) override
    {
        if (event.type == InputEvent::CURSOR && isDragging)
        {
            //if (event.cursor != lastCursor)
               // myArcball.mouse_drag(event.cursor, event.windowSize);
        }
        
        if (event.type == InputEvent::MOUSE)
        {
            if (event.is_mouse_down())
            {
                isDragging = true;
                //myArcball.mouse_down(event.cursor, event.windowSize);
            }
            
            if (event.is_mouse_up())
            {
                isDragging = false;
                //myArcball.mouse_down(event.cursor, event.windowSize);
            }
        }
        
        cameraController.handle_input(event);
        lastCursor = event.cursor;
    }
    
    void on_update(const UpdateEvent & e) override
    {
        cameraController.update(e.elapsed_s / 1000);
    }

    
    void on_draw() override
    {
        static int frameCount = 0;
        
        glfwMakeContextCurrent(window);
        
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
     
        const auto proj = camera.get_projection_matrix((float) width / (float) height);
        const float4x4 view = camera.get_view_matrix();
        const float4x4 viewProj = mul(proj, view);
        
        {
            hosek_sky->bind();
            
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
        
            float3 sunDirection = spherical(to_radians(sunTheta), to_radians(sunPhi));
            
            // Largest non-clipped sphere
            float4x4 world = make_translation_matrix(camera.get_eye_point()) * make_scaling_matrix(camera.farClip * .99);
            world *= make_rotation_matrix({1, 0, 0}, to_radians(-90));
            
            hosek_sky->uniform("ViewProjection", viewProj);
            hosek_sky->uniform("World", world);
            
            hosek_sky->uniform("A", sky.A);
            hosek_sky->uniform("B", sky.B);
            hosek_sky->uniform("C", sky.C);
            hosek_sky->uniform("D", sky.D);
            hosek_sky->uniform("E", sky.E);
            hosek_sky->uniform("F", sky.F);
            hosek_sky->uniform("G", sky.G);
            hosek_sky->uniform("H", sky.H);
            hosek_sky->uniform("I", sky.I);
            hosek_sky->uniform("Z", sky.Z);
            hosek_sky->uniform("SunDirection", sunDirection);
            
            //glDisable(GL_DEPTH_TEST);
            //glDepthMask(GL_FALSE);
            
            skyMesh.draw_elements();
            
            //glDepthMask(GL_TRUE);
            //glEnable(GL_BLEND);
            //glEnable(GL_CULL_FACE);

            hosek_sky->unbind();
        }
            
        {
            simpleShader->bind();
            
            simpleShader->uniform("u_viewProj", viewProj);
            simpleShader->uniform("u_eye", float3(0, 10, -10));
            
            simpleShader->uniform("u_emissive", float3(.33f, 0.36f, 0.275f));
            simpleShader->uniform("u_diffuse", float3(0.2f, 0.4f, 0.25f));
            
            simpleShader->uniform("u_lights[0].position", float3(5, 10, -5));
            simpleShader->uniform("u_lights[0].color", float3(0.7f, 0.2f, 0.2f));
            
            simpleShader->uniform("u_lights[1].position", float3(-5, 10, 5));
            simpleShader->uniform("u_lights[1].color", float3(0.4f, 0.8f, 0.4f));
            
            {
                sofaModel.pose.position = float3(0, -1, -4);
                //sofaModel.pose.orientation = qmul(myArcball.get_quat(), sofaModel.pose.orientation);
                
                //std::cout <<  sofaModel.pose.orientation << std::endl;
                
                auto model = mul(sofaModel.pose.matrix(), make_scaling_matrix(0.001));
                
                simpleShader->uniform("u_modelMatrix", model);
                simpleShader->uniform("u_modelMatrixIT", inv(transpose(model)));
                sofaModel.draw();
            }
            
            {
                auto model = make_scaling_matrix(1);
                simpleShader->uniform("u_modelMatrix", model);
                simpleShader->uniform("u_modelMatrixIT", inv(transpose(model)));
                //skyMesh.draw_elements();
            }
            
            simpleShader->unbind();
        }
        
        grid.render(proj, view);
        
        gfx::gl_check_error(__FILE__, __LINE__);
        
        for (auto widget : rootWidget.children)
        {
            //myTexture->draw(widget->bounds, math::int2{width, height});
        }

        gfx::gl_check_error(__FILE__, __LINE__);
        
        glfwSwapBuffers(window);
        
        frameCount++;
    }
    
};

IMPLEMENT_MAIN(int argc, char * argv[])
{
    ExperimentalApp app;
    app.main_loop();
    return 0;
}

