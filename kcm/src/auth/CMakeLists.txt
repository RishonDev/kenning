kauth_install_actions(org.kde.kcontrol.kcmfaceunlock kcm_faceunlock.actions)

add_executable(kcmfaceunlock_authhelper faceunlockhelper.cpp faceunlockhelper.h)
target_include_directories(kcmfaceunlock_authhelper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kcmfaceunlock_authhelper KF6::AuthCore KF6::ConfigCore KF6::CoreAddons)

kauth_install_helper_files(kcmfaceunlock_authhelper org.kde.kcontrol.kcmfaceunlock root)
install(TARGETS kcmfaceunlock_authhelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
