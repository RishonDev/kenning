cmake_minimum_required(VERSION 3.20)
project(faceunlock VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(OpenCV REQUIRED COMPONENTS core imgproc objdetect videoio dnn)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_library(PAM_LIBRARY pam REQUIRED)

set(FACEUNLOCK_BINARY ${CMAKE_INSTALL_FULL_BINDIR}/faceunlock)
set(FACEUNLOCK_SYSCONFDIR ${CMAKE_INSTALL_FULL_SYSCONFDIR}/faceunlock CACHE PATH "Config and face models")
set(FACEUNLOCK_DATADIR ${CMAKE_INSTALL_FULL_DATADIR}/plasma-faceunlock CACHE PATH "Recognition networks")
add_compile_definitions(FACEUNLOCK_SYSCONFDIR="${FACEUNLOCK_SYSCONFDIR}" FACEUNLOCK_DATADIR="${FACEUNLOCK_DATADIR}")

add_library(faceunlock-engine STATIC src/engine.cpp src/engine.h src/config.h)
target_link_libraries(faceunlock-engine PUBLIC ${OpenCV_LIBS} Qt6::Core)
target_include_directories(faceunlock-engine PUBLIC ${OpenCV_INCLUDE_DIRS} src)
set_target_properties(faceunlock-engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(faceunlock src/main.cpp)
target_compile_definitions(faceunlock PRIVATE FACEUNLOCK_VERSION="${PROJECT_VERSION}")
target_link_libraries(faceunlock PRIVATE faceunlock-engine)

# The PAM module only needs config.h; OpenCV stays out of the programs that authenticate.
add_library(pam_faceunlock MODULE src/pam_faceunlock.cpp src/config.h)
set_target_properties(pam_faceunlock PROPERTIES PREFIX "")
target_compile_definitions(pam_faceunlock PRIVATE FACEUNLOCK_BINARY="${FACEUNLOCK_BINARY}")
target_include_directories(pam_faceunlock PRIVATE src)
target_link_libraries(pam_faceunlock PRIVATE ${PAM_LIBRARY})

install(TARGETS faceunlock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS pam_faceunlock LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/security)
install(FILES config.ini DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/faceunlock)
install(FILES models/LICENSE.yunet models/LICENSE.sface DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma-faceunlock/models)
