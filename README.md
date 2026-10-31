# Kenning

Face unlock for KDE Plasma: a System Settings page, an animated face scan on the
login and lock screens, and installable animation themes. (To *ken* someone is to
know or recognise them.)

- `kcm/` – System Settings > Security & Privacy > Face Unlock, its KAuth helper
  and the packaged themes (`kcm/themes/`, format in [THEMES.md](THEMES.md))
- `lockscreen/` – adds the face scan to the Plasma lock screen; re-applied by a
  pacman hook after plasma-desktop updates and undone on removal
- `engine/native/` – the face recognition engine: `kenning` (enrol, list,
  remove, compare) on OpenCV's YuNet detector and SFace recognizer, and
  `pam_kenning.so`, which asks the Kenning service (`kenning daemon`) so
  OpenCV never loads into sudo, the lock screen or the login manager. Config
  keys, model files, exit codes and PAM messages follow Howdy.
- `login-manager/` – patch for Plasma Login Manager: scans as soon as the login
  screen appears and shows the animation
- `packaging/` – pacman hooks, install script (PAM tidying, migration from the
  earlier `plasma-faceunlock` packages) and the first-startup setup prompt

## Packages

Everything comes from the distributions' own repositories (OpenCV, Qt, KDE
Frameworks); the two recognition networks (YuNet, MIT; SFace, Apache-2.0) are
downloaded once, next to the sources: `makepkg` does it on Arch, elsewhere fetch
`face_detection_yunet_2023mar.onnx` and `face_recognition_sface_2021dec.onnx` from
[OpenCV Zoo](https://github.com/opencv/opencv_zoo) into the repository root.

| Distribution | Build | Notes |
|---|---|---|
| Arch Linux | `makepkg -si` | Also builds `plasma-login-manager-kenning`, for face unlock on the login screen |
| Debian, Ubuntu | `cp -r packaging/debian debian && dpkg-buildpackage -b -uc -us -jauto` | |
| Fedora | `rpmbuild -bb packaging/rpm/kenning.spec` (sources as in the spec) | |

All of them share one CMake install (`CMakeLists.txt`); `tests/containers/` builds
and tests them in containers. On Plasma 6.8 and newer the lock screen uses Plasma's
own Face sign-in method: Kenning provides its PAM service, `kde-face` (made from the
distribution's `kde-fingerprint` with `pam_kenning.so` in place of `pam_fprintd.so`),
and the lock screen add-on adds the scan animation, the fallback to the password and
fingerprint, and switching to the password when you type. On Plasma 6.3 to 6.7 face
unlock runs before the password check in the `kde` service; the add-on adds the scan
animation there too, and unlocks without pressing Unlock. An unknown Plasma version
keeps its stock lock screen, which still unlocks with your face after pressing Unlock. Face unlock on the login screen needs `plasma-login-manager-kenning`,
packaged for Arch only so far.

Then open System Settings > Face Unlock and press Set Up: the IR camera is found
automatically (without one, setup offers the ordinary webcam with a warning:
it is less secure and needs your face lit from the front) and face unlock is switched on for the login screen, lock screen
and sudo after the first face is saved.

The native engine replaces Howdy. The `archive/howdy-bundled` branch keeps a
version with Howdy's dlib engine vendored as a fallback (existing Howdy face
models keep working, but it needs `python-dlib` from the AUR). Faces enrolled
with Howdy have to be enrolled again here.

Face models are readable only by root. Face checks go through a small service
(`kenning daemon`, started on demand by systemd via /run/kenning.sock) that
owns the camera and the models, like fprintd does for fingerprints; it answers only
for root or for the user's own account.
