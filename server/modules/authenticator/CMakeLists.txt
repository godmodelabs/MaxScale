add_library(MySQLAuth SHARED mysql_auth.c)
target_link_libraries(MySQLAuth maxscale-common)
set_target_properties(MySQLAuth PROPERTIES VERSION "1.0.0")
install_module(MySQLAuth core)

add_library(NullAuthAllow SHARED null_auth_allow.c)
target_link_libraries(NullAuthAllow maxscale-common)
set_target_properties(NullAuthAllow PROPERTIES VERSION "1.0.0")
install_module(NullAuthAllow core)

add_library(NullAuthDeny SHARED null_auth_deny.c)
target_link_libraries(NullAuthDeny maxscale-common)
set_target_properties(NullAuthDeny PROPERTIES VERSION "1.0.0")
install_module(NullAuthDeny core)

add_library(MaxAdminAuth SHARED max_admin_auth.c)
target_link_libraries(MaxAdminAuth maxscale-common)
set_target_properties(MaxAdminAuth PROPERTIES VERSION "1.0.0")
install_module(MaxAdminAuth core)

add_library(HTTPAuth SHARED http_auth.c)
target_link_libraries(HTTPAuth maxscale-common)
set_target_properties(HTTPAuth PROPERTIES VERSION "1.0.0")
install_module(HTTPAuth core)

# if(BUILD_TESTS)
#   add_library(testprotocol SHARED testprotocol.c)
#   set_target_properties(testprotocol PROPERTIES VERSION "1.0.0")
#   target_link_libraries(testprotocol maxscale-common)
#   install_module(testprotocol core)
# endif()
if(BUILD_CDC)
  add_library(CDCPlainAuth SHARED cdc_plain_auth.c)
  target_link_libraries(CDCPlainAuth maxscale-common)
  set_target_properties(CDCPlainAuth PROPERTIES VERSION "1.0.0")
  install_module(CDCPlainAuth core)
endif()

