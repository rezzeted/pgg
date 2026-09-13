#include "pch.h"

#include "PreviewCapture.h"

#include <cstring>
#include <spdlog/spdlog.h>
#include <sokol_gfx.h>

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
#elif defined(SOKOL_METAL) && defined(__APPLE__)
    #import <Metal/Metal.h>
    #import <Foundation/Foundation.h>
#elif defined(SOKOL_D3D11)
    #include <d3d11.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

void swizzleBgraToRgba(std::vector<std::uint8_t>& pixels) {
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
}

void flipVertically(std::vector<std::uint8_t>& pixels, int width, int height) {
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
    for (int y = 0; y < height / 2; ++y) {
        std::uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        std::uint8_t* bot = pixels.data() + static_cast<std::size_t>(height - 1 - y) * width * 4;
        std::memcpy(row.data(), top, row.size());
        std::memcpy(top, bot, row.size());
        std::memcpy(bot, row.data(), row.size());
    }
}

}  // namespace

bool writePngRgba(const char* path, int width, int height, const std::vector<std::uint8_t>& pixels) {
    if (!path || path[0] == '\0' || width <= 0 || height <= 0) return false;
    const int ok = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    if (!ok) {
        spdlog::error("writePngRgba: stbi_write_png failed for {}", path);
        return false;
    }
    return true;
}

PreviewCaptureResult capturePreview(const GeometryPreview& preview) {
    PreviewCaptureResult res;
    const int width = preview.targetWidth();
    const int height = preview.targetHeight();
    const sg_image img = preview.resolvedColorImage();
    if (width <= 0 || height <= 0 || img.id == SG_INVALID_ID) {
        spdlog::error("capturePreview: no preview target");
        return res;
    }
    res.sizeClamped = preview.targetSizeClamped();
    std::vector<std::uint8_t> pixels;

#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    const sg_gl_image_info gi = sg_gl_query_image_info(img);
    const GLuint tex = gi.tex[gi.active_slot];
    if (tex == 0) {
        spdlog::error("capturePreview: GL texture id is 0");
        return res;
    }
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("capturePreview: GL framebuffer incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &fbo);
        return res;
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &fbo);
    if (!sg_query_features().origin_top_left) flipVertically(pixels, width, height);

#elif defined(SOKOL_METAL) && defined(__APPLE__)
    const sg_mtl_image_info mi = sg_mtl_query_image_info(img);
    id<MTLTexture> src = (__bridge id<MTLTexture>)mi.tex[mi.active_slot];
    if (src == nil) {
        spdlog::error("capturePreview: Metal texture is nil");
        return res;
    }
    id<MTLDevice> device = src.device;
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat
                                                                                    width:static_cast<NSUInteger>(width)
                                                                                   height:static_cast<NSUInteger>(height)
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> dst = [device newTextureWithDescriptor:desc];
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(static_cast<NSUInteger>(width), static_cast<NSUInteger>(height), 1)
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (cmd.status == MTLCommandBufferStatusError) {
        spdlog::error("capturePreview: Metal blit failed");
        return res;
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    [dst getBytes:pixels.data()
      bytesPerRow:static_cast<NSUInteger>(width * 4)
       fromRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height))
      mipmapLevel:0];
    if (src.pixelFormat == MTLPixelFormatBGRA8Unorm || src.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB)
        swizzleBgraToRgba(pixels);

#elif defined(SOKOL_D3D11)
    const sg_d3d11_image_info di = sg_d3d11_query_image_info(img);
    ID3D11Texture2D* tex = const_cast<ID3D11Texture2D*>(static_cast<const ID3D11Texture2D*>(di.tex2d));
    if (tex == nullptr) {
        spdlog::error("capturePreview: D3D11 texture is null");
        return res;
    }
    ID3D11Device* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    ID3D11DeviceContext* context =
        static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    D3D11_TEXTURE2D_DESC desc = {};
    tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr) || staging == nullptr) {
        spdlog::error("capturePreview: D3D11 staging texture creation failed");
        return res;
    }
    context->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::error("capturePreview: D3D11 Map failed");
        staging->Release();
        return res;
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y) {
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4,
                    srcRow + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * 4);
    }
    context->Unmap(staging, 0);
    staging->Release();
    swizzleBgraToRgba(pixels);
#else
    spdlog::error("capturePreview: backend has no FBO readback");
    return res;
#endif

    res.ok = true;
    res.width = width;
    res.height = height;
    res.pixels = std::move(pixels);
    return res;
}
