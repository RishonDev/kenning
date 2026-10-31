# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: CC0-1.0
#
# Shared by the settings page and its root helper.

add_library(kenning_settings_common STATIC
    camerafinder.cpp camerafinder.h
    lockscreenstate.cpp lockscreenstate.h
    paths.h
    settingsschema.cpp settingsschema.h
)
set_target_properties(kenning_settings_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kenning_settings_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kenning_settings_common PUBLIC Qt6::Core kenning-config)
