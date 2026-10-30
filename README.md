# plasma-faceunlock

Face unlock for KDE Plasma: a System Settings page, an
animated face scan on the login and lock screens, and installable animation
themes.

- `kcm/` – System Settings > Security & Privacy > Face Unlock, its KAuth helper
  and the packaged themes (`kcm/themes/`, format in [THEMES.md](THEMES.md))
- `lockscreen/` – adds the face scan to the Plasma lock screen; re-applied by a
  pacman hook after plasma-desktop updates and undone on removal
- `login-manager/` – patch and PKGBUILD for Plasma Login Manager: scans as soon
  as the login screen appears and shows the animation
- `packaging/` – pacman hooks

Build with `makepkg -si` here, and in `login-manager/` for the login screen.
