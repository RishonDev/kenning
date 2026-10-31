# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: CC0-1.0
#
# Kenning in one build, used by every package format:
#   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DKENNING_NETWORKS_DIR=<dir with the .onnx files>
#   cmake --build build && DESTDIR=... cmake --install build
# Distribution-specific pieces (pacman hooks, dpkg and RPM triggers, install
# scripts) live with each package format under packaging/.

cmake_minimum_required(VERSION 3.20)
project(kenning VERSION 0.6.0 LANGUAGES CXX)
include(GNUInstallDirs)

option(KENNING_BUILD_SETTINGS "Build the System Settings page (needs KDE Frameworks 6)" ON)
set(KENNING_NETWORKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Directory with face_detection_yunet_2023mar.onnx and face_recognition_sface_2021dec.onnx")

add_subdirectory(engine/native)
if(KENNING_BUILD_SETTINGS)
    add_subdirectory(kcm)
endif()

# The recognition networks (YuNet, MIT; SFace, Apache-2.0) from OpenCV Zoo; they
# are too large for the repository, so packages download them.
foreach(network face_detection_yunet_2023mar.onnx face_recognition_sface_2021dec.onnx)
    if(NOT EXISTS "${KENNING_NETWORKS_DIR}/${network}")
        message(FATAL_ERROR "${network} not found in KENNING_NETWORKS_DIR (${KENNING_NETWORKS_DIR}). "
                            "Download it from https://github.com/opencv/opencv_zoo (models/face_detection_yunet, models/face_recognition_sface).")
    endif()
    # Package sources are often symlinks (makepkg's are); install the file itself.
    file(REAL_PATH "${KENNING_NETWORKS_DIR}/${network}" network_file)
    install(FILES "${network_file}" DESTINATION ${CMAKE_INSTALL_DATADIR}/kenning/models RENAME ${network})
endforeach()

# Scripts the packages and the lock screen add-on call by this fixed path.
install(PROGRAMS packaging/pam-tidy packaging/setup-prompt lockscreen/kenning-lockscreen.py DESTINATION lib/kenning)
install(FILES kcm/src/ui/FaceScanIndicator.qml DESTINATION lib/kenning)
install(FILES packaging/org.kde.kenning.setupprompt.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

# The service socket is on by default (pam_kenning needs it); nothing runs until used.
set(wants "lib/systemd/system/sockets.target.wants")
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${wants}\")
    execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink ../kenning.socket
                            \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${wants}/kenning.socket\")
")
