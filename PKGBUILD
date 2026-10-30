# Face unlock for Plasma in one build: `makepkg -si` installs both packages.
#   plasma-faceunlock                Howdy face recognition (vendored in engine/howdy),
#                                    System Settings page, lock screen add-on, themes
#   plasma-login-manager-faceunlock  Plasma Login Manager that scans as soon as it appears

pkgbase=plasma-faceunlock
pkgname=(plasma-faceunlock plasma-login-manager-faceunlock)
pkgver=0.3.0
_plmver=6.8.2
pkgrel=1
arch=(x86_64)
url='https://github.com/boltgolt/howdy'
license=(GPL-2.0-or-later MIT)
makedepends=(cmake extra-cmake-modules meson ninja qt6-tools
             # plasma-login-manager
             kauth kcmutils kconfig kcoreaddons kdbusaddons ki18n kio kirigami kpackage kservice
             layer-shell-qt libkscreen libplasma libxau pam plasma-workspace qt6-5compat qt6-base qt6-declarative systemd-libs
             # howdy
             libinih libevdev)
source=(https://download.kde.org/stable/plasma/$_plmver/plasma-login-manager-$_plmver.tar.xz{,.sig}
        https://github.com/davisking/dlib-models/raw/master/dlib_face_recognition_resnet_model_v1.dat.bz2
        https://github.com/davisking/dlib-models/raw/master/mmod_human_face_detector.dat.bz2
        https://github.com/davisking/dlib-models/raw/master/shape_predictor_5_face_landmarks.dat.bz2)
sha256sums=('4a6e2b65b5d462fcf43b2a5be3c3e6244f479451b6dcfa00dcbf33fbc5cd093a'
            'SKIP'
            'abb1f61041e434465855ce81c2bd546e830d28bcbed8d27ffbe5bb408b11553a'
            'db9e9e40f092c118d5eb3e643935b216838170793559515541c56a2b50d9fc84'
            '6e787bbebf5c9efdb793f6cd1f023230c4413306605f24f299f12869f95aa472')
validpgpkeys=('E0A3EB202F8E57528E13E72FD7574483BB57B18D'  # Jonathan Esk-Riddell <jr@jriddell.org>
              '0AAC775BB6437A8D9AF7A3ACFE0784117FBCE11D'  # Bhushan Shah <bshah@kde.org>
              'D07BD8662C56CB291B316EB2F5675605C74E02CF'  # David Edmundson <davidedmundson@kde.org>
              '1FA881591C26B276D7A5518EEAAF29B42A678C20') # Marco Martin <notmart@gmail.com>

prepare() {
  patch -d plasma-login-manager-$_plmver -p1 < "$startdir/login-manager/howdy-auto-login.patch"
}

build() {
  arch-meson "$startdir/engine/howdy" howdy-build -Dpam_dir=/usr/lib/security
  meson compile -C howdy-build

  cmake -B kcm-build -S "$startdir/kcm" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKDE_INSTALL_LIBEXECDIR=lib
  cmake --build kcm-build

  cmake -B plm-build -S plasma-login-manager-$_plmver -DKDE_INSTALL_LIBEXECDIR=lib \
    -DDBUS_CONFIG_FILENAME=plasma_org.freedesktop.DisplayManager.conf
  cmake --build plm-build
}

package_plasma-faceunlock() {
  pkgdesc='Windows Hello style face unlock for Plasma, with Howdy built in'
  depends=(kauth kcmutils kconfig kcoreaddons ki18n kirigami libplasma plasma-desktop qt6-declarative
           libarchive libinih libevdev python python-dlib python-opencv python-numpy)
  optdepends=('plasma-login-manager-faceunlock: start face unlock as soon as the login screen appears')
  provides=(howdy)
  conflicts=(howdy howdy-git)
  backup=(etc/howdy/config.ini)

  meson install -C howdy-build --destdir "$pkgdir"
  install -Dm644 "$startdir/engine/howdy/howdy/src/config.ini" "$pkgdir/etc/howdy/config.ini"
  install -Dm644 "$startdir/engine/howdy/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE.howdy"
  install -Dm644 -t "$pkgdir/usr/share/dlib-data" *.dat
  # polkit 127+ runs its agent helper sandboxed; let it reach the camera (boltgolt/howdy#1077)
  install -Dm644 /dev/stdin "$pkgdir/usr/lib/systemd/system/polkit-agent-helper@.service.d/10-faceunlock.conf" <<CONF
[Service]
PrivateDevices=no
DeviceAllow=char-video4linux rw
DeviceAllow=/dev/uinput rw
CONF

  DESTDIR="$pkgdir" cmake --install kcm-build
  install -Dm755 "$startdir/lockscreen/faceunlock-lockscreen.py" "$pkgdir/usr/lib/plasma-faceunlock/faceunlock-lockscreen.py"
  install -Dm644 "$startdir/kcm/src/ui/FaceScanIndicator.qml" "$pkgdir/usr/lib/plasma-faceunlock/FaceScanIndicator.qml"
  install -Dm644 -t "$pkgdir/usr/share/libalpm/hooks" "$startdir"/packaging/*.hook
}

package_plasma-login-manager-faceunlock() {
  pkgdesc='Plasma Login Manager that starts face unlock as soon as it appears'
  depends=(glibc kauth kcmutils kconfig kcoreaddons kdbusaddons ki18n kio kirigami kpackage kservice layer-shell-qt
           libkscreen libplasma libstdc++ libxau pam plasma-workspace qt6-5compat qt6-base qt6-declarative sh systemd-libs)
  optdepends=('plasma-faceunlock: face unlock settings and themes')
  provides=("plasma-login-manager=$_plmver")
  conflicts=(plasma-login-manager)
  groups=(plasma)

  DESTDIR="$pkgdir" cmake --install plm-build
}
