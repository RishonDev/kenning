kauth_install_actions(org.kde.kcontrol.kcmkenning kcm_kenning.actions)

add_executable(kcmkenning_authhelper faceunlockhelper.cpp faceunlockhelper.h)
target_include_directories(kcmkenning_authhelper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kcmkenning_authhelper KF6::AuthCore KF6::ConfigCore KF6::CoreAddons)

kauth_install_helper_files(kcmkenning_authhelper org.kde.kcontrol.kcmkenning root)
install(TARGETS kcmkenning_authhelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
