file(GLOB HEADERS "*.h")
foreach(var ${HEADERS})
  get_filename_component(header ${var} NAME)
  install_header(${header} devel)
endforeach()

add_subdirectory(maxscale)
