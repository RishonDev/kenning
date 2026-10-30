# Roadmap

In order. Each step builds on the one before.

## 1. Choose the engine: Howdy or native

Two branches exist: `howdy-bundled` (Howdy 3 vendored, needs python-dlib from
the AUR) and `native-engine` (C++ on OpenCV's YuNet and SFace, official
packages only). The native engine loads in ~60 ms against Howdy's ~370 ms, but
a scan currently takes slightly longer, probably because YuNet detects faces
less readily in IR frames.

- [ ] Add a timing breakdown to `faceunlock test`: time to the first lit frame,
      frames with a detected face, time to a match
- [ ] Close the speed gap: boost contrast on IR frames (CLAHE) before
      detection, tune the YuNet threshold for IR, load the networks while the
      camera warms up
- [ ] Compare both over a few days: unlock time, failed unlocks, install
      effort, fit for KDE upstream
- [ ] Merge the chosen branch into `master`; keep the other as an archive branch

## 2. Warning labels for risky options

Show a short warning (Kirigami.InlineMessage or a ContextualHelpButton) where an
option trades security or privacy for convenience:

- [ ] Administrator prompts and sudo: your face can approve changes to the system
- [ ] Match threshold set lenient: people who look like you may get in
- [ ] "Don't use when the lid is closed" or "Don't use over SSH" turned off
- [ ] Dismiss the lock screen automatically: unlocks as soon as you are seen,
      without a key press
- [ ] Take a photo of failed attempts: stores pictures of whoever tries
- [ ] "Say when face recognition starts" turned off: the login and lock screen
      animation depends on it

## 3. Laptops with only an ordinary (colour) webcam

Setup currently requires an IR camera. Without one, offer the ordinary webcam:

- [ ] Setup finds the colour webcam when there is no IR camera and explains,
      with a warning to acknowledge, that it is less accurate and can be fooled
      by a photo of you
- [ ] In that mode, default to the lock and login screens only; sudo and
      administrator prompts stay on the password
- [ ] Try both approaches and keep the more reliable one:
  - mimic IR: greyscale plus brightness and contrast normalisation (CLAHE), so
    the existing pipeline sees IR-like frames even in dim light
  - use the colour image directly: YuNet and SFace were trained on colour
    photos and may match better without the greyscale step
- [ ] Later: a liveness check (blink or a head turn) to make photos useless

## Follow-ups (not yet ordered)

- [ ] native engine: "Take a photo of failed attempts" does nothing yet;
      implement it or hide the option
- [ ] A root service that owns the camera and face models (like fprintd), so
      models no longer have to be readable by local users
- [ ] Upstream route: a `kde-face` PAM service and a face authenticator in
      kscreenlocker beside fingerprint; the login manager auto-scan as a merge
      request; the settings page as a KCM or part of Users
- [ ] A trademark-safe product name before any commercial release
