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

`makepkg -si` builds and installs both packages: `kenning` and
`plasma-login-manager-kenning` (replaces plasma-login-manager). Everything
comes from the official Arch repositories (opencv, Qt, KDE Frameworks); the two
networks (YuNet, MIT; SFace, Apache-2.0) are downloaded at build time.
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
