# SPDX-FileCopyrightText: 2026 Rishon Jonathan R
# SPDX-License-Identifier: CC0-1.0

Name:           kenning
Version:        0.4.0
Release:        1%{?dist}
Summary:        Face unlock for KDE Plasma
License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND MIT AND Apache-2.0
URL:            https://github.com/RishonDev/kenning
Source0:        %{name}-%{version}.tar.gz
Source1:        https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
Source2:        https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx

BuildRequires:  cmake
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  kf6-kauth-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  opencv-devel
BuildRequires:  pam-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  systemd-devel
BuildRequires:  systemd-rpm-macros

Requires:       bsdtar
Requires:       kf6-kcmutils
Requires:       kf6-kirigami
Requires:       libnotify
Requires:       python3
Requires:       qt6-qtdeclarative
Requires:       systemd
Recommends:     plasma-desktop

%description
Kenning signs you in with your face: on the lock screen, for sudo and, with its
version of Plasma Login Manager, on the login screen. It works best with an
infrared camera; an ordinary webcam can be used with a warning.

It includes a System Settings page to set it up, an animated face scan on the
lock screen (Plasma 6.8 or newer) and installable animation themes. Recognition
runs in a small service that owns the camera and the face models.

%prep
%autosetup
cp %{SOURCE1} %{SOURCE2} .

%build
%cmake -DKENNING_NETWORKS_DIR=%{_builddir}/%{name}-%{version}
%cmake_build

%install
%cmake_install

%post
# The service user owns the face models, which nobody else can read.
systemd-sysusers kenning.conf >/dev/null 2>&1 || :
mkdir -p %{_sysconfdir}/kenning/models
chown -R kenning:kenning %{_sysconfdir}/kenning/models || :
chmod 700 %{_sysconfdir}/kenning/models
find %{_sysconfdir}/kenning/models -type f -exec chmod 600 {} +
/usr/lib/kenning/pam-tidy tidy || :
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || :
    systemctl start kenning.socket || :
    systemctl try-restart kenning.service || :
fi

%preun
if [ $1 -eq 0 ]; then
    # Nothing may point at pam_kenning.so or the add-on once they are gone.
    /usr/lib/kenning/pam-tidy remove || :
    python3 -I /usr/lib/kenning/kenning-lockscreen.py --restore || :
    if [ -d /run/systemd/system ]; then
        systemctl stop kenning.socket kenning.service || :
    fi
fi

# Re-apply the lock screen add-on whenever Plasma's lock screen files change,
# including when this package is installed after Plasma.
%transfiletriggerin -- /usr/share/plasma/shells/org.kde.plasma.desktop/contents/lockscreen
python3 -I /usr/lib/kenning/kenning-lockscreen.py || :

%transfiletriggerin -- /usr/lib/kenning
python3 -I /usr/lib/kenning/kenning-lockscreen.py || :

%files
%license LICENSES/* engine/native/models/LICENSE.yunet engine/native/models/LICENSE.sface
%doc README.md THEMES.md
%{_bindir}/kenning
%{_libdir}/security/pam_kenning.so
%{_unitdir}/kenning.service
%{_unitdir}/kenning.socket
%{_unitdir}/sockets.target.wants/kenning.socket
%{_sysusersdir}/kenning.conf
%dir %{_sysconfdir}/kenning
%config(noreplace) %{_sysconfdir}/kenning/config.ini
%config(noreplace) %{_sysconfdir}/xdg/autostart/org.kde.kenning.setupprompt.desktop
%{_datadir}/kenning/
/usr/lib/kenning/
%{_qt6_plugindir}/plasma/kcms/systemsettings/kcm_kenning.so
%{_libexecdir}/kf6/kauth/kcmkenning_authhelper
%{_datadir}/dbus-1/system-services/org.kde.kcontrol.kcmkenning.service
%{_datadir}/dbus-1/system.d/org.kde.kcontrol.kcmkenning.conf
%{_datadir}/polkit-1/actions/org.kde.kcontrol.kcmkenning.policy
%{_datadir}/applications/kcm_kenning.desktop

%changelog
* Fri Oct 30 2026 Rishon Jonathan R <kenning@invalid> - 0.4.0-1
- First Fedora package
