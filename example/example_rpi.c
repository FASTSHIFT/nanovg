#include "nanovg.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#include <stdio.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include "demo.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
#include "perf.h"

#include <time.h>

static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;

static int blowup = 0;
static int premult = 0;

static int init_egl(int width, int height)
{
    static EGL_DISPMANX_WINDOW_T nativewindow;

    bcm_host_init();

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display: %d\n", eglGetError());
        return -1;
    }

    if (!eglInitialize(display, NULL, NULL)) {
        printf("Failed to initialize EGL: %d\n", eglGetError());
        return -1;
    }

    EGLint attribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(display, attribs, &config, 1, &num_configs)) {
        printf("Failed to choose EGL config: %d\n", eglGetError());
        return -1;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        printf("Failed to create EGL context: %d\n", eglGetError());
        return -1;
    }

    // Create dispmanx window
    VC_RECT_T dst_rect = { 0, 0, width, height };
    VC_RECT_T src_rect = { 0, 0, width << 16, height << 16 };

    DISPMANX_DISPLAY_HANDLE_T dispman_display = vc_dispmanx_display_open(0);
    DISPMANX_UPDATE_HANDLE_T dispman_update = vc_dispmanx_update_start(0);
    DISPMANX_ELEMENT_HANDLE_T dispman_element = vc_dispmanx_element_add(
        dispman_update, dispman_display,
        0, &dst_rect, 0,
        &src_rect, DISPMANX_PROTECTION_NONE,
        0, 0, DISPMANX_NO_ROTATE);

    nativewindow.element = dispman_element;
    nativewindow.width = width;
    nativewindow.height = height;
    vc_dispmanx_update_submit_sync(dispman_update);

    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    if (surface == EGL_NO_SURFACE) {
        printf("Failed to create EGL surface: %d\n", eglGetError());
        return -1;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("Failed to make EGL context current: %d\n", eglGetError());
        return -1;
    }

    return 0;
}

static float getTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float)ts.tv_sec + (float)ts.tv_nsec / 1000000000.0f;
}

int main()
{
    DemoData data;
    NVGcontext* vg = NULL;
    PerfGraph fps;
    double prevt = 0;
    int width = 480, height = 480;

    if (init_egl(width, height) == -1) {
        return -1;
    }
    initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");

    vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
    if (vg == NULL) {
        printf("Could not init nanovg.\n");
        return -1;
    }

    if (loadDemoData(vg, &data) == -1)
        return -1;

    prevt = getTime();

    while (1) {
        double mx = width / 2, my = height / 2, t, dt;
        float pxRatio = 1.0f;

        t = getTime();
        dt = t - prevt;
        prevt = t;
        updateGraph(&fps, dt);

        // Update and render
        glViewport(0, 0, width, height);
        if (premult)
            glClearColor(0, 0, 0, 0);
        else
            glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        nvgBeginFrame(vg, width, height, pxRatio);
        renderDemo(vg, mx, my, width, height, t, blowup, &data);
        renderGraph(vg, 5, 5, &fps);
        nvgEndFrame(vg);

        GLenum gl_err = glGetError();
        if (gl_err != GL_NO_ERROR) {
            printf("GLES error after nvgEndFrame: 0x%X\n", gl_err);
        }

        eglSwapBuffers(display, surface);
    }

    freeDemoData(vg, &data);
    nvgDeleteGLES2(vg);
    eglTerminate(display);
    bcm_host_deinit();
    return 0;
}
