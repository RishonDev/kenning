kcmutils_add_qml_kcm(kcm_kenning)

target_sources(kcm_kenning PRIVATE
    faceunlockkcm.cpp faceunlockkcm.h
    camerapreview.cpp camerapreview.h
    faceunlockpaths.h camerafinder.h
)

target_link_libraries(kcm_kenning PRIVATE
    Qt6::DBus
    Qt6::Quick
    KF6::AuthCore
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtilsQuick
)

add_subdirectory(auth)
