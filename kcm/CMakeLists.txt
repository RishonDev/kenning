cmake_minimum_required(VERSION 3.20)
project(kcm_faceunlock VERSION 0.1.0)

set(QT_MIN_VERSION "6.8.0")
set(KF6_MIN_VERSION "6.10.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Auth Config CoreAddons I18n KCMUtils)

add_definitions(-DTRANSLATION_DOMAIN="kcm_faceunlock")

add_subdirectory(src)


install(DIRECTORY themes/ DESTINATION ${KDE_INSTALL_DATADIR}/faceunlock/themes)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
