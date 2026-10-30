# plasma-faceunlock

Face unlock for KDE Plasma: a System Settings page, an
animated face scan on the login and lock screens, and installable animation
themes.

- `kcm/` – System Settings > Security & Privacy > Face Unlock, its KAuth helper
  and the packaged themes (`kcm/themes/`, format in [THEMES.md](THEMES.md))
- `lockscreen/` – adds the face scan to the Plasma lock screen; re-applied by a
  pacman hook after plasma-desktop updates and undone on removal
- `engine/native/` – the face recognition engine: `faceunlock` (enrol, list,
  remove, compare) on OpenCV's YuNet detector and SFace recognizer, and
  `pam_faceunlock.so`, which runs `faceunlock compare` in its own process so
  OpenCV never loads into sudo, the lock screen or the login manager. Config
  keys, model files, exit codes and PAM messages follow Howdy.
- `login-manager/` – patch for Plasma Login Manager: scans as soon as the login
  screen appears and shows the animation
- `packaging/` – pacman hooks

`makepkg -si` builds and installs both packages: `plasma-faceunlock` and
`plasma-login-manager-faceunlock` (replaces plasma-login-manager). Everything
comes from the official Arch repositories (opencv, Qt, KDE Frameworks); the two
networks (YuNet, MIT; SFace, Apache-2.0) are downloaded at build time.
Then open System Settings > Face Unlock and press Set Up: the IR camera is found
automatically and face unlock is switched on for the login screen, lock screen
and sudo after the first face is saved.

This branch (`native-engine`) replaces Howdy. The `howdy-bundled` branch keeps
Howdy's dlib engine instead, so existing Howdy face models keep working, but it
needs `python-dlib` from the AUR. Faces enrolled with Howdy have to be enrolled
again here.

Known limitation: the lock screen authenticates as the user, so face models in
/etc/faceunlock/models are readable by local users. A system service that owns the
camera and models (like fprintd) would remove that.
