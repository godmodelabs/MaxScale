# Set the install layout
include(GNUInstallDirs)

set(MAXSCALE_LIBDIR ${CMAKE_INSTALL_LIBDIR}/maxscale CACHE PATH "Library installation path")
set(MAXSCALE_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE PATH "Executable installation path")
set(MAXSCALE_SHAREDIR ${CMAKE_INSTALL_DATADIR}/maxscale CACHE PATH "Share file installation path, includes licence and readme files")
set(MAXSCALE_DOCDIR ${CMAKE_INSTALL_DOCDIR}/maxscale CACHE PATH "Documentation installation path, text versions only")

# These are the only hard-coded absolute paths
set(MAXSCALE_VARDIR /var CACHE PATH "Data file path (usually /var/)")
set(MAXSCALE_CONFDIR /etc CACHE PATH "Configuration file installation path (/etc/)")

# Massage TARGET_COMPONENT into a list
if (TARGET_COMPONENT)
  string(REPLACE "," ";" TARGET_COMPONENT ${TARGET_COMPONENT})
  list(FIND TARGET_COMPONENT "all" BUILD_ALL)
endif()

#
# Installation functions for MaxScale
#
# Do not directly install files with install(...) etc. commands. Use these
# functions for all executables, modules and files that are to be installed
# as a part of a package. These functions make additional checks that all
# required parameters are present for the modules and make sure that the
# targets are installed into right directories where MaxScale can find them.
#

# Installation functions for executables and modules.
#
# @param Name of the CMake target
# @param Component where this executable should be included
function(install_executable target component)
  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(TARGETS ${target} DESTINATION ${MAXSCALE_BINDIR} COMPONENT "${component}")
  endif()

endfunction()

# Installation function for modules
#
# @param Name of the CMake target
# @param Component where this module should be included
function(install_module target component)
  get_target_property(TGT_VERSION ${target} VERSION)

  if (${TGT_VERSION} MATCHES "NOTFOUND")
    message(AUTHOR_WARNING "Module '${target}' is missing the VERSION parameter!")
  endif()

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(TARGETS ${target} DESTINATION ${MAXSCALE_LIBDIR} COMPONENT "${component}")
  endif()

endfunction()

# Installation functions for interpreted scripts.
#
# @param Script to install
# @param Component where this script should be included
function(install_script target component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(PROGRAMS ${target} DESTINATION ${MAXSCALE_BINDIR} COMPONENT "${component}")
  endif()

endfunction()

# Installation functions for files and programs. These all go to the share directory
# of MaxScale which for packages is /usr/share/maxscale.
#
# @param File to install
# @param Component where this file should be included
function(install_file file component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)
  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(FILES ${file} DESTINATION ${MAXSCALE_SHAREDIR} COMPONENT "${component}")
  endif()
endfunction()

function(install_program file component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(PROGRAMS ${file} DESTINATION ${MAXSCALE_SHAREDIR} COMPONENT "${component}")
  endif()

endfunction()

# Install man pages
#
# @param Manual file to install
# @param The page number where this should be installed e.g. man1
# @param Component where this manual should be included
function(install_manual file page component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(PROGRAMS ${file} DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man${page} COMPONENT "${component}")
  endif()

endfunction()

# Install headers
#
# @param Header to install
# @param Component where this header should be included
function(install_header header component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(FILES ${header} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/maxscale COMPONENT "${component}")
  endif()

endfunction()


# Install custom file to a custom destination
#
# @param File to install
# @param Destination where to install the file
# @param Component where this file should be included
function(install_custom_file file dest component)

  list(FIND TARGET_COMPONENT ${component} BUILD_COMPONENT)

  if(BUILD_COMPONENT GREATER -1 OR BUILD_ALL GREATER -1)
    install(FILES ${file} DESTINATION ${dest} COMPONENT "${component}")
  endif()

endfunction()
