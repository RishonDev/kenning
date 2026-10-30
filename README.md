# plasma-faceunlock

Windows Hello style face unlock for KDE Plasma: a System Settings page, an
animated face scan on the login and lock screens, and installable animation
themes.

- `kcm/` – System Settings > Security & Privacy > Face Unlock, its KAuth helper
  and the packaged themes (`kcm/themes/`, format in [THEMES.md](THEMES.md))
- `lockscreen/` – adds the face scan to the Plasma lock screen; re-applied by a
  pacman hook after plasma-desktop updates and undone on removal
- `engine/howdy/` – [Howdy](https://github.com/boltgolt/howdy) 3 (MIT), vendored
  at d3ab993 without howdy-gtk; does the face recognition and provides pam_howdy
- `login-manager/` – patch for Plasma Login Manager: scans as soon as the login
  screen appears and shows the animation
- `packaging/` – pacman hooks

`makepkg -si` builds and installs both packages: `plasma-faceunlock` (replaces
howdy) and `plasma-login-manager-faceunlock` (replaces plasma-login-manager).
Then open System Settings > Face Unlock and press Set Up: the IR camera is found
automatically and face unlock is switched on for the login screen, lock screen
and sudo after the first face is saved.

This branch (`howdy-bundled`) keeps Howdy's dlib engine, so existing Howdy face
models keep working; it needs `python-dlib` from the AUR. The `native-engine`
branch replaces it with a C++ engine on OpenCV that needs only official
packages.

Known limitation: the lock screen authenticates as the user, so face models in
/etc/howdy/models are readable by local users.
