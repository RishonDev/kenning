cmake_minimum_required(VERSION 3.20)
project(kenning VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(OpenCV REQUIRED COMPONENTS core imgproc objdetect videoio dnn)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_library(PAM_LIBRARY pam REQUIRED)
# logind tells the PAM module whether a request comes from a remote session
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSYSTEMD IMPORTED_TARGET libsystemd)
endif()

set(KENNING_BINARY ${CMAKE_INSTALL_FULL_BINDIR}/kenning)
set(KENNING_SYSCONFDIR ${CMAKE_INSTALL_FULL_SYSCONFDIR}/kenning CACHE PATH "Config and face models")
set(KENNING_DATADIR ${CMAKE_INSTALL_FULL_DATADIR}/kenning CACHE PATH "Recognition networks")
add_compile_definitions(KENNING_SYSCONFDIR="${KENNING_SYSCONFDIR}" KENNING_DATADIR="${KENNING_DATADIR}")

add_library(kenning-engine STATIC src/engine.cpp src/engine.h src/config.h)
target_link_libraries(kenning-engine PUBLIC ${OpenCV_LIBS} Qt6::Core)
target_include_directories(kenning-engine PUBLIC ${OpenCV_INCLUDE_DIRS} src)
set_target_properties(kenning-engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(KENNING_SOCKET /run/kenning.sock CACHE STRING "Socket of the face unlock service")
add_compile_definitions(KENNING_SOCKET="${KENNING_SOCKET}")
set(KENNING_SERVICE_USER kenning CACHE STRING "System user the service runs as and that owns the face models")
add_compile_definitions(KENNING_SERVICE_USER="${KENNING_SERVICE_USER}")

add_executable(kenning src/main.cpp src/daemon.cpp src/daemon.h src/service.h)
target_compile_definitions(kenning PRIVATE KENNING_VERSION="${PROJECT_VERSION}")
target_link_libraries(kenning PRIVATE kenning-engine)

# The PAM module only asks the service; OpenCV stays out of the programs that authenticate.
add_library(pam_kenning MODULE src/pam_kenning.cpp src/config.h src/service.h)
set_target_properties(pam_kenning PROPERTIES PREFIX "")
target_include_directories(pam_kenning PRIVATE src)
target_link_libraries(pam_kenning PRIVATE ${PAM_LIBRARY})
if(LIBSYSTEMD_FOUND)
    target_compile_definitions(pam_kenning PRIVATE KENNING_HAVE_LOGIND)
    target_link_libraries(pam_kenning PRIVATE PkgConfig::LIBSYSTEMD)
else()
    message(WARNING "libsystemd not found: remote sessions are only recognised by SSH_* variables")
endif()

install(TARGETS kenning RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS pam_kenning LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/security)
configure_file(systemd/kenning.service.in kenning.service @ONLY)
configure_file(systemd/kenning-sysusers.conf.in kenning-sysusers.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kenning-sysusers.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sysusers.d RENAME kenning.conf)
configure_file(systemd/kenning.socket.in kenning.socket @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kenning.service ${CMAKE_CURRENT_BINARY_DIR}/kenning.socket
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES config.ini DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/kenning)
install(FILES models/LICENSE.yunet models/LICENSE.sface DESTINATION ${CMAKE_INSTALL_DATADIR}/kenning/models)
