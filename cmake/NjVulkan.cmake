# Optional headless Vulkan rasterizer. Shader binaries ship in the C source tree.
option(NJ_ENABLE_VULKAN "Enable the Vulkan M3G triangle backend" OFF)
get_filename_component(_nj_vulkan_dir "${CMAKE_CURRENT_LIST_DIR}/../src/port/vulkan" ABSOLUTE)
if(NJ_ENABLE_VULKAN)
  find_package(Vulkan REQUIRED)
endif()

function(nj_enable_vulkan target)
  if(NJ_ENABLE_VULKAN)
    target_sources(${target} PRIVATE "${_nj_vulkan_dir}/nj_gfx_vulkan.c"
      "${_nj_vulkan_dir}/nj_vk_ahb_probe.c")
    target_compile_definitions(${target} PUBLIC NJ_ENABLE_VULKAN=1)
    target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
  endif()
endfunction()
