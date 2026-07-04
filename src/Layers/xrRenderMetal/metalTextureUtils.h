#pragma once

#ifdef USE_METAL

#include <Metal/Metal.hpp>

#include <gli/format.hpp>
#include <gli/target.hpp>

namespace xray::render::RENDER_NAMESPACE
{
namespace metalTextureUtils
{
// D3DFORMAT -> MTL::PixelFormat (render targets and engine-created surfaces).
// Note: Apple Silicon has no 24-bit depth formats, so D3DFMT_D24S8/D24X8
// map to MTL::PixelFormatDepth32Float_Stencil8.
MTL::PixelFormat ConvertTextureFormat(D3DFORMAT dx9FMT);

// gli/DDS format -> MTL::PixelFormat (disk texture loading).
// Returns MTL::PixelFormatInvalid for formats Metal cannot sample directly
// (e.g. 24-bit RGB8/BGR8 — those must be expanded to 32-bit on the CPU).
MTL::PixelFormat ConvertGLIFormat(gli::format fmt);

// gli target -> MTL::TextureType.
MTL::TextureType ConvertTextureTarget(gli::target target);

// true when fmt is a block-compressed (BCn) format.
bool IsCompressed(MTL::PixelFormat fmt);

// Bytes per 4x4 block for BCn formats, bytes per pixel otherwise.
u32 BytesPerBlock(MTL::PixelFormat fmt);
} // namespace metalTextureUtils
} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
