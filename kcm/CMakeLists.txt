# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: CC0-1.0

set(QT_MIN_VERSION "6.8.0")
set(KF6_MIN_VERSION "6.10.0")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core DBus Gui Qml Quick)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Auth Config CoreAddons I18n KCMUtils)

add_definitions(-DTRANSLATION_DOMAIN="kcm_kenning")

add_subdirectory(src)

install(DIRECTORY themes/ DESTINATION ${KDE_INSTALL_DATADIR}/kenning/themes)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
