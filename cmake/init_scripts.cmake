# Check which init.d script to install
find_file(RPM_FNC functions PATHS /etc/rc.d/init.d)
find_file(DEB_FNC init-functions PATHS /lib/lsb)

if(${RPM_FNC} MATCHES "NOTFOUND" AND ${DEB_FNC} MATCHES "NOTFOUND")
  message(FATAL_ERROR "Cannot find required init-functions in /lib/lsb/ or /etc/rc.d/init.d/, please confirm that your system files are OK.")
elseif(${RPM_FNC} MATCHES "NOTFOUND")
  set(HAVE_LSB_FUNCTIONS TRUE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
else()
  set(HAVE_LSB_FUNCTIONS FALSE CACHE BOOL "If init.d script uses /lib/lsb/init-functions instead of /etc/rc.d/init.d/functions.")
endif()

configure_file(${CMAKE_SOURCE_DIR}/etc/maxscale.conf.in ${CMAKE_BINARY_DIR}/maxscale.conf @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/etc/maxscale.service.in ${CMAKE_BINARY_DIR}/maxscale.service @ONLY)

if(HAVE_LSB_FUNCTIONS)
  configure_file(${CMAKE_SOURCE_DIR}/etc/ubuntu/init.d/maxscale.in ${CMAKE_BINARY_DIR}/maxscale @ONLY)
else()
  configure_file(${CMAKE_SOURCE_DIR}/etc/init.d/maxscale.in ${CMAKE_BINARY_DIR}/maxscale @ONLY)
endif()

if(PACKAGE)
  message(STATUS "maxscale.conf will unpack to: /etc/ld.so.conf.d")
  message(STATUS "startup scripts will unpack to to: /etc/init.d")
  message(STATUS "systemd service files will unpack to to: /usr/lib/systemd/system")
  install_file(${CMAKE_BINARY_DIR}/maxscale core)
  install_file(${CMAKE_BINARY_DIR}/maxscale.conf core)
  install_file(${CMAKE_BINARY_DIR}/maxscale.service core)
else()
  install(PROGRAMS ${CMAKE_BINARY_DIR}/maxscale DESTINATION /etc/init.d COMPONENT core)
  install(FILES ${CMAKE_BINARY_DIR}/maxscale.conf  DESTINATION /etc/ld.so.conf.d COMPONENT core)
  install(FILES ${CMAKE_BINARY_DIR}/maxscale.service  DESTINATION /usr/lib/systemd/system COMPONENT core)
  message(STATUS "Installing maxscale.conf to: /etc/ld.so.conf.d")
  message(STATUS "Installing startup scripts to: /etc/init.d")
  message(STATUS "Installing systemd service files to: /usr/lib/systemd/system")
endif()
