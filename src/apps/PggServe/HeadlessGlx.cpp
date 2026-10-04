#include "pch.h"

#include "HeadlessGlx.h"

#if defined(__linux__)

    #include <GL/glx.h>

    // glxext.h clashes with sokol_app.h's own GLX typedefs elsewhere, so the
    // four ARB_create_context constants are defined here when missing.
    #ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
        #define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
    #endif
    #ifndef GLX_CONTEXT_MINOR_VERSION_ARB
        #define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
    #endif
    #ifndef GLX_CONTEXT_PROFILE_MASK_ARB
        #define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
    #endif
    #ifndef GLX_CONTEXT_CORE_PROFILE_BIT_ARB
        #define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
    #endif

#endif

bool pggHeadlessGlSetup(PggHeadlessGl& gl, std::string& err) {
#if defined(__linux__)
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        err = "XOpenDisplay failed: no DISPLAY (run under xvfb-run -a)";
        return false;
    }
    static int fbAttrs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
                            GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
                            GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
                            GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, None};
    int n = 0;
    GLXFBConfig* cfgs = glXChooseFBConfig(dpy, DefaultScreen(dpy), fbAttrs, &n);
    if (!cfgs || n == 0) {
        err = "glXChooseFBConfig: no pbuffer-capable config";
        XCloseDisplay(dpy);
        return false;
    }
    GLXFBConfig fb = cfgs[0];
    XFree(cfgs);
    int pbAttrs[] = {GLX_PBUFFER_WIDTH, 64, GLX_PBUFFER_HEIGHT, 64, None};
    gl.pbuffer = glXCreatePbuffer(dpy, fb, pbAttrs);
    if (!gl.pbuffer) {
        err = "glXCreatePbuffer failed";
        XCloseDisplay(dpy);
        return false;
    }
    using CreateCtxFn = GLXContext (*)(Display*, GLXFBConfig, GLXContext, int, const int*);
    auto createCtx = reinterpret_cast<CreateCtxFn>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    if (!createCtx) {
        err = "glXCreateContextAttribsARB not found";
        glXDestroyPbuffer(dpy, gl.pbuffer);
        XCloseDisplay(dpy);
        return false;
    }
    int ctxAttrs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                      GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None};
    gl.context = createCtx(dpy, fb, nullptr, True, ctxAttrs);
    if (!gl.context) {
        err = "GL 3.3 core context creation failed";
        glXDestroyPbuffer(dpy, gl.pbuffer);
        XCloseDisplay(dpy);
        return false;
    }
    if (!glXMakeContextCurrent(dpy, gl.pbuffer, gl.pbuffer, static_cast<GLXContext>(gl.context))) {
        err = "glXMakeContextCurrent failed";
        glXDestroyContext(dpy, static_cast<GLXContext>(gl.context));
        glXDestroyPbuffer(dpy, gl.pbuffer);
        XCloseDisplay(dpy);
        return false;
    }
    gl.display = dpy;
    return true;
#else
    (void)gl;
    err = "headless GL context is implemented for Linux only";
    return false;
#endif
}

void pggHeadlessGlTeardown(PggHeadlessGl& gl) {
#if defined(__linux__)
    Display* dpy = static_cast<Display*>(gl.display);
    if (!dpy) return;
    glXMakeContextCurrent(dpy, 0, 0, nullptr);
    if (gl.context) glXDestroyContext(dpy, static_cast<GLXContext>(gl.context));
    if (gl.pbuffer) glXDestroyPbuffer(dpy, gl.pbuffer);
    XCloseDisplay(dpy);
#endif
    gl = PggHeadlessGl{};
}
