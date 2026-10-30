kcmutils_add_qml_kcm(kcm_faceunlock)

target_sources(kcm_faceunlock PRIVATE
    faceunlockkcm.cpp faceunlockkcm.h
    camerapreview.cpp camerapreview.h
    faceunlockpaths.h
)

target_link_libraries(kcm_faceunlock PRIVATE
    Qt6::Quick
    KF6::AuthCore
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtilsQuick
)

add_subdirectory(auth)
