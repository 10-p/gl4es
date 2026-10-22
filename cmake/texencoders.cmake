# The `texencoders` static library: ARM astc-encoder (Apache-2.0) and etcpak (BSD-3-Clause), fetched at pinned
# commits, only their codec sources compiled (neither project's own CMakeLists.txt is used). Each picks its SIMD
# paths from the compiler's target macros. A project that also builds it (engine-ut99's XOpenGLDrv glue) makes the
# same target; whichever comes first is used, so the encoders are linked once.
if(NOT TARGET texencoders)
    enable_language(CXX)
    include(FetchContent)
    set(ETCPAK_GIT_REPOSITORY "https://github.com/wolfpld/etcpak.git" CACHE STRING "etcpak git repo")
    set(ETCPAK_GIT_TAG "3d716e1550023dc5ff8b602636af8b76584e0b66" CACHE STRING "etcpak commit")
    set(ASTCENC_GIT_REPOSITORY "https://github.com/ARM-software/astc-encoder.git" CACHE STRING "astc-encoder git repo")
    set(ASTCENC_GIT_TAG "baff485b0ff36d2f95d28961605106502c653966" CACHE STRING "astc-encoder commit (5.7.0)")
    FetchContent_Declare(etcpak
        GIT_REPOSITORY "${ETCPAK_GIT_REPOSITORY}" GIT_TAG "${ETCPAK_GIT_TAG}" SOURCE_SUBDIR no-cmake-build)
    FetchContent_Declare(astcenc
        GIT_REPOSITORY "${ASTCENC_GIT_REPOSITORY}" GIT_TAG "${ASTCENC_GIT_TAG}" SOURCE_SUBDIR no-cmake-build)
    FetchContent_MakeAvailable(etcpak astcenc)
    file(GLOB ASTCENC_SOURCES "${astcenc_SOURCE_DIR}/Source/astcenc_*.cpp")
    add_library(texencoders STATIC
        ${etcpak_SOURCE_DIR}/ProcessRGB.cpp ${etcpak_SOURCE_DIR}/Tables.cpp ${etcpak_SOURCE_DIR}/Dither.cpp
        ${ASTCENC_SOURCES})
    target_include_directories(texencoders PUBLIC ${etcpak_SOURCE_DIR} ${astcenc_SOURCE_DIR}/Source)
    # In its own build directory: gl4es sends archives to ${CMAKE_SOURCE_DIR}/lib, the parent project's tree when
    # it is fetched.
    set_target_properties(texencoders PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
