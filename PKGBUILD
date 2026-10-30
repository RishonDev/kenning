# Kenning, face unlock for Plasma, in one build: `makepkg -si` installs both packages.
#   kenning                       face recognition engine and service (engine/native, OpenCV),
#                                 System Settings page, lock screen add-on, themes
#   plasma-login-manager-kenning  Plasma Login Manager that scans as soon as it appears

pkgbase=kenning
pkgname=(kenning plasma-login-manager-kenning)
pkgver=0.4.0
_plmver=6.8.2
pkgrel=2
arch=(x86_64)
url='https://invent.kde.org/'
license=(GPL-2.0-or-later MIT Apache-2.0)
makedepends=(cmake extra-cmake-modules meson ninja qt6-tools
             # plasma-login-manager
             kauth kcmutils kconfig kcoreaddons kdbusaddons ki18n kio kirigami kpackage kservice
             layer-shell-qt libkscreen libplasma libxau pam plasma-workspace qt6-5compat qt6-base qt6-declarative systemd-libs
             # engine
             opencv)
source=(https://download.kde.org/stable/plasma/$_plmver/plasma-login-manager-$_plmver.tar.xz{,.sig}
        https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
        https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx)
sha256sums=('4a6e2b65b5d462fcf43b2a5be3c3e6244f479451b6dcfa00dcbf33fbc5cd093a'
            'SKIP'
            '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4'
            '0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79')
validpgpkeys=('E0A3EB202F8E57528E13E72FD7574483BB57B18D'  # Jonathan Esk-Riddell <jr@jriddell.org>
              '0AAC775BB6437A8D9AF7A3ACFE0784117FBCE11D'  # Bhushan Shah <bshah@kde.org>
              'D07BD8662C56CB291B316EB2F5675605C74E02CF'  # David Edmundson <davidedmundson@kde.org>
              '1FA881591C26B276D7A5518EEAAF29B42A678C20') # Marco Martin <notmart@gmail.com>

prepare() {
  patch -d plasma-login-manager-$_plmver -p1 < "$startdir/login-manager/plasma-login-manager-kenning.patch"
}

build() {
  cmake -B engine-build -S "$startdir/engine/native" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SYSCONFDIR=/etc \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build engine-build

  cmake -B kcm-build -S "$startdir/kcm" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKDE_INSTALL_LIBEXECDIR=lib
  cmake --build kcm-build

  cmake -B plm-build -S plasma-login-manager-$_plmver -DKDE_INSTALL_LIBEXECDIR=lib \
    -DDBUS_CONFIG_FILENAME=plasma_org.freedesktop.DisplayManager.conf
  cmake --build plm-build
}

package_kenning() {
  pkgdesc='Kenning: face unlock for KDE Plasma'
  depends=(kauth kcmutils kconfig kcoreaddons ki18n kirigami libplasma plasma-desktop qt6-base qt6-declarative
           libarchive libnotify opencv pam python)
  optdepends=('plasma-login-manager-kenning: start face unlock as soon as the login screen appears')
  backup=(etc/kenning/config.ini)
  # Earlier name; the install script moves its settings and face models across
  conflicts=(plasma-faceunlock)
  replaces=(plasma-faceunlock)
  install=packaging/kenning.install

  DESTDIR="$pkgdir" cmake --install engine-build
  install -Dm644 -t "$pkgdir/usr/share/kenning/models" *.onnx
  # The service socket is on by default (the PAM module needs it); nothing runs until used
  install -d "$pkgdir/usr/lib/systemd/system/sockets.target.wants"
  ln -s ../kenning.socket "$pkgdir/usr/lib/systemd/system/sockets.target.wants/kenning.socket"

  DESTDIR="$pkgdir" cmake --install kcm-build
  install -Dm755 "$startdir/lockscreen/kenning-lockscreen.py" "$pkgdir/usr/lib/kenning/kenning-lockscreen.py"
  install -Dm644 "$startdir/kcm/src/ui/FaceScanIndicator.qml" "$pkgdir/usr/lib/kenning/FaceScanIndicator.qml"
  install -Dm644 -t "$pkgdir/usr/share/libalpm/hooks" "$startdir"/packaging/*.hook
  install -Dm755 -t "$pkgdir/usr/lib/kenning" "$startdir/packaging/pam-tidy" "$startdir/packaging/setup-prompt"
  install -Dm644 -t "$pkgdir/etc/xdg/autostart" "$startdir/packaging/org.kde.kenning.setupprompt.desktop"
}

package_plasma-login-manager-kenning() {
  pkgdesc='Plasma Login Manager that starts face unlock as soon as it appears'
  depends=(glibc kauth kcmutils kconfig kcoreaddons kdbusaddons ki18n kio kirigami kpackage kservice layer-shell-qt
           libkscreen libplasma libstdc++ libxau pam plasma-workspace qt6-5compat qt6-base qt6-declarative sh systemd-libs)
  optdepends=('kenning: face unlock settings and themes')
  provides=("plasma-login-manager=$_plmver")
  conflicts=(plasma-login-manager plasma-login-manager-faceunlock)
  replaces=(plasma-login-manager-faceunlock)
  groups=(plasma)

  DESTDIR="$pkgdir" cmake --install plm-build
}
