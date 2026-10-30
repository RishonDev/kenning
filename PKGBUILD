# Face unlock for Plasma: System Settings page, root helper and lock screen add-on.
# Build from this directory with makepkg; the login screen fork is in login-manager/.

pkgname=plasma-faceunlock
pkgver=0.2.1
pkgrel=1
pkgdesc='Face unlock (Howdy) settings for Plasma, with Windows Hello style lock screen animation'
arch=(x86_64)
license=(GPL-2.0-or-later)
depends=(kauth kcmutils kconfig kcoreaddons ki18n kirigami libplasma plasma-desktop python qt6-declarative libarchive)
makedepends=(cmake extra-cmake-modules)
optdepends=('howdy-git: face recognition backend'
            'plasma-login-manager: login screen face unlock (use the patched build in login-manager/)')

build() {
  cmake -B build -S "$startdir/kcm" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKDE_INSTALL_LIBEXECDIR=lib
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
  install -Dm755 "$startdir/lockscreen/faceunlock-lockscreen.py" "$pkgdir/usr/lib/plasma-faceunlock/faceunlock-lockscreen.py"
  install -Dm644 "$startdir/kcm/src/ui/FaceScanIndicator.qml" "$pkgdir/usr/lib/plasma-faceunlock/FaceScanIndicator.qml"
  install -Dm644 -t "$pkgdir/usr/share/libalpm/hooks" "$startdir"/packaging/*.hook
}
