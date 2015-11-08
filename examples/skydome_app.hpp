#include "anvil.hpp"

using namespace math;
using namespace util;
using namespace gfx;

struct ExperimentalApp : public GLFWApp
{
    uint64_t frameCount = 0;

    GlCamera camera;
    
    float sunTheta = 80.f;
    PreethamProceduralSky preethamSky;
    HosekProceduralSky hosekWilkieSky;
    
    ProceduralSky * sky;
    
    RenderableGrid grid;

    FPSCameraController cameraController;
    
    bool useHdr = true;
    float hdrExposure = 1.0f;
    std::unique_ptr<GlShader> hdrShader;
    GlMesh fullscreen_post_quad;
    
    GlFramebuffer sceneFramebuffer;
    GlTexture sceneColorTexture;
    
    GlFramebuffer hdrOutputFramebuffer;
    GlTexture hdrOutputTexture;
    
    std::unique_ptr<GLTextureView> sceneView;
    
    ExperimentalApp() : GLFWApp(600, 600, "Skydome Example App")
    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        grid = RenderableGrid(1, 100, 100);
        cameraController.set_camera(&camera);
        gfx::gl_check_error(__FILE__, __LINE__);
        
        sky = dynamic_cast<PreethamProceduralSky *>(&preethamSky);
        
        hdrShader.reset(new gfx::GlShader(read_file_text("assets/shaders/post_vertex.glsl"), read_file_text("assets/shaders/hdr_frag.glsl")));
        fullscreen_post_quad = make_fullscreen_quad();
        
        sceneColorTexture.load_data(width, height, GL_RGB16F, GL_RGB, GL_FLOAT, nullptr);
        sceneFramebuffer.attach(GL_COLOR_ATTACHMENT0, sceneColorTexture);
        if (!sceneFramebuffer.check_complete()) throw std::runtime_error("incomplete framebuffer");
        
        hdrOutputTexture.load_data(width, height, GL_RGB, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        hdrOutputFramebuffer.attach(GL_COLOR_ATTACHMENT0, hdrOutputTexture);
        if (!hdrOutputFramebuffer.check_complete()) throw std::runtime_error("incomplete framebuffer");
        
        sceneView.reset(new GLTextureView(hdrOutputTexture.get_gl_handle()));
    }
    
    void on_window_resize(math::int2 size) override
    {

    }
   
    void on_input(const InputEvent & event) override
    {
        if (event.type == InputEvent::KEY)
        {
            if (event.value[0] == GLFW_KEY_1 && event.action == GLFW_RELEASE)
            {
                sky = dynamic_cast<PreethamProceduralSky *>(&preethamSky);
            }
            else if (event.value[0] == GLFW_KEY_2 && event.action == GLFW_RELEASE)
            {
                sky = dynamic_cast<HosekProceduralSky *>(&hosekWilkieSky);
            }
            else if (event.value[0] == GLFW_KEY_UP && event.action == GLFW_RELEASE)
            {
                hdrExposure += 0.2f;
            }
            else if (event.value[0] == GLFW_KEY_DOWN && event.action == GLFW_RELEASE)
            {
                hdrExposure += -0.2f;
            }
            else if (event.value[0] == GLFW_KEY_H && event.action == GLFW_RELEASE)
            {
                useHdr = !useHdr;
                if (useHdr)sceneView.reset(new GLTextureView(hdrOutputTexture.get_gl_handle()));
                else sceneView.reset(new GLTextureView(sceneColorTexture.get_gl_handle()));
            }
            else if (event.value[0] == GLFW_KEY_EQUAL && event.action == GLFW_RELEASE)
            {
                sunTheta += 5;
                sky->recompute(sunTheta, 4, 0.1, 1.15);
            }
            else if (event.value[0] == GLFW_KEY_MINUS && event.action == GLFW_RELEASE)
            {
                sunTheta -= 5;
                sky->recompute(sunTheta, 4, 0.1, 1.15);
            }
        }
        
        cameraController.handle_input(event);
    }
    
    void on_update(const UpdateEvent & e) override
    {
        cameraController.update(e.timestep_ms);
    }
    
    void on_draw() override
    {
        glfwMakeContextCurrent(window);
        
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
    
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        
        const auto proj = camera.get_projection_matrix((float) width / (float) height);
        const float4x4 view = camera.get_view_matrix();
        const float4x4 viewProj = mul(proj, view);
        
        sceneFramebuffer.bind_to_draw();
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            sky->render(viewProj, camera.get_eye_point(), camera.farClip);
            grid.render(proj, view);
            gfx::gl_check_error(__FILE__, __LINE__);
        }
        
        // Draw into the output FBO
        hdrOutputFramebuffer.bind_to_draw();
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            hdrShader->bind();
            hdrShader->texture("u_Texture", 0, sceneColorTexture);
            hdrShader->uniform("u_Exposure", hdrExposure);
            
            // Passthrough geometry
            fullscreen_post_quad.draw_elements();
            
            hdrShader->unbind();
        }
        
        // Bind to 0
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        sceneView->draw({0.f, 0.f, (float) width, (float) height}, {width, height});
        
        glfwSwapBuffers(window);
        
        frameCount++;
    }
    
};