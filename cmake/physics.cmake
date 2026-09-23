include(FetchContent)

function(sengine_build_physics)
  set(ENABLE_INSTALL ON)
  set(DOUBLE_PRECISION OFF)
  set(OVERRIDE_CXX_FLAGS OFF)
  set(INTERPROCEDURAL_OPTIMIZATION OFF)
  set(GENERATE_DEBUG_SYMBOLS OFF)
  set(CPP_EXCEPTIONS_ENABLED ON)
  set(CPP_RTTI_ENABLED ON)
  set(ENABLE_ALL_WARNINGS OFF)
  set(CROSS_PLATFORM_DETERMINISTIC ON)
  set(JPH_BUILD_SHARED_LIBS OFF)
  set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
  set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
  set(ENABLE_OBJECT_STREAM OFF)
  set(JPH_USE_DX12 OFF)
  set(JPH_USE_VK OFF)
  set(JPH_USE_MTL OFF)
  set(JPH_USE_CPU_COMPUTE OFF)
  foreach(feature SSE4_1 SSE4_2 AVX AVX2 AVX512 LZCNT TZCNT F16C FMADD)
    set(USE_${feature} OFF)
  endforeach()
  FetchContent_Declare(sengine_jolt
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    SOURCE_SUBDIR Build
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(sengine_jolt)
  set_target_properties(Jolt PROPERTIES POSITION_INDEPENDENT_CODE ON DISABLE_PRECOMPILE_HEADERS ON)
  install(FILES "${sengine_jolt_SOURCE_DIR}/LICENSE" DESTINATION share/sengine/licenses/jolt)
endfunction()

sengine_build_physics()
