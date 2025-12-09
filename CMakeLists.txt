# CMakeList.txt : CMake project for VideoPlayer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("videoplayer" VERSION 0.1.0)

option(WITH_PDB "Generate windows PDB file" OFF)

set(TARGET videoplayer)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(FFMPEG_DIR "${PROJECT_SOURCE_DIR}/libs/ffmpeg")

include_directories(
  ${FFMPEG_DIR}/include
)

if (MSVC)
  set(MSVC_STATIC_CRT OFF)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /bigobj /utf-8")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /bigobj /utf-8")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /bigobj /utf-8")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /bigobj /utf-8")

  link_directories(
    ${FFMPEG_DIR}/lib/Win64
  )

  set(VERSIONINFO_RC ${CMAKE_BINARY_DIR}/VersionInfo.rc)
  configure_file(${CMAKE_SOURCE_DIR}/VersionInfo.rc.in ${VERSIONINFO_RC})

  if (WITH_PDB)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
  endif()
else()
    set(VERSIONINFO_RC "")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

If (ANDROID)
  link_directories(
    ${FFMPEG_DIR}/lib/Android/${ANDROID_ABI}
  )
  
  set(ANDROID_STL c++_static)
endif()

file(GLOB_RECURSE SRC_FILES "src/*.cpp")
file(GLOB_RECURSE HEADER_FILES "src/*.h")

message(STATUS "Find  ${SRC_FILES} files.")
message(STATUS "Find ${HEADER_FILES} head files.")

add_library(
    ${TARGET} SHARED
    ${SRC_FILES}
    ${VERSIONINFO_RC}
)

target_sources(${TARGET} PRIVATE ${HEADER_FILES})

target_compile_definitions(${TARGET}
    PRIVATE VIDEO_PLAYER_EXPORTS
)

if (WIN32)
    target_compile_definitions(${TARGET} PRIVATE
        AVFORMAT_STATIC
        AVUTIL_STATIC
        AVCODEC_STATIC
        SWSCALE_STATIC
    )

    target_link_libraries(${TARGET} PRIVATE
    # FFmpeg 内部依赖（顺序不严格）
    libzlib.lib
    libbz2.lib
    liblzma.lib
    
    # FFmpeg libs - 顺序必须从高层到低层
    libavutil.lib
    libswresample.lib
    libswscale.lib
    libavcodec.lib 
    libavformat.lib
    # disabled: libavfilter.lib
    # disabled: libpostproc.lib
)
else()
    target_link_libraries(${TARGET} PRIVATE
        libavutil.a
        libswresample.a
        libswscale.a
        libavcodec.a 
        libavformat.a
    )
endif()


if(WIN32)
    set(LIB_INSTALL_DIR "lib/Win64")
elseif(ANDROID)
    set(LIB_INSTALL_DIR "lib/Android/${ANDROID_ABI}")
else()
    set(LIB_INSTALL_DIR "lib")
endif()

install(TARGETS ${TARGET} 
   LIBRARY DESTINATION ${LIB_INSTALL_DIR}
   ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
   RUNTIME DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${PROJECT_SOURCE_DIR}/src/videoplayer_c_api.h DESTINATION include)