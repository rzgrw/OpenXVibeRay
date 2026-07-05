if (XRAY_USE_LUAJIT)
    add_subdirectory(LuaJIT-proj)
else()
    find_package(Lua51 REQUIRED)
endif()

add_subdirectory(luabind)
add_subdirectory(xrLuaFix)
add_subdirectory(OPCODE)
add_subdirectory(ode)

# Silence all warnings for external libraries
add_compile_options(${XRAY_DISABLE_WARNINGS})

add_subdirectory(GameSpy)
add_subdirectory(imgui-proj)

if (NOT TARGET xrLuabind)
    message(FATAL_ERROR
        "You probably have downloaded OpenXRay sources as ZIP archive, you can't do that. Use git to clone the repository.\n"
        "Read the build instructions: https://github.com/OpenXRay/xray-16/wiki"
    )
endif()

if (XRAY_METAL OR XRAY_VULKAN)
    # Shader front-end toolchain, vendored source drops pinned at tag
    # vulkan-sdk-1.3.290.0 (re-pin to one current LunarG SDK tag when the
    # Vulkan deps land — see specs/2026-07-05-engine-roadmap-v2.md).
    #   glslang     — GLSL -> SPIR-V. Needed by the Metal path AND the
    #                 Vulkan renderer (SPIR-V is Vulkan's native IR), on
    #                 ALL platforms once render_vulkan exists.
    #   SPIRV-Cross — SPIR-V -> MSL. ONLY the parked Metal backend needs it.
    # Neither builds in the default configuration (both renderer options
    # default OFF), so default builds don't pay for ~7k vendored files.
    # Directory-scope overrides: static libs, out of the engine unity build.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_UNITY_BUILD OFF)
endif()

if (XRAY_METAL)
    set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE) # MSL backend builds on the GLSL one
    set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(SPIRV-Cross)
endif()

if (XRAY_METAL OR XRAY_VULKAN)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE) # glslang: no External/ dependencies checked out
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE) # would require SPIRV-Tools
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(glslang)
endif()
