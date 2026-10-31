kauth_install_actions(org.kde.kcontrol.kcmkenning kcm_kenning.actions)

find_package(LibArchive REQUIRED)

add_executable(kcmkenning_authhelper faceunlockhelper.cpp faceunlockhelper.h)
target_link_libraries(kcmkenning_authhelper kenning_settings_common KF6::AuthCore KF6::ConfigCore KF6::CoreAddons LibArchive::LibArchive)

kauth_install_helper_files(kcmkenning_authhelper org.kde.kcontrol.kcmkenning root)
install(TARGETS kcmkenning_authhelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
