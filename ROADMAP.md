# Roadmap

In order. Each step builds on the one before.

## 1. Choose the engine: Howdy or native — decided: native

The native engine (C++ on OpenCV's YuNet and SFace, official packages only) is
now `master`. Howdy 3 vendored (needs python-dlib from the AUR) is kept on
`archive/howdy-bundled` as a fallback; it lacks fixes made after the decision,
such as skipping the face scan when a password was typed at the login screen.

Measured on 2026-10-30 (Asus Vivobook 16, 640x360 GREY IR camera, same seat,
alternating runs): native compare 1.65-1.68 s and recognised every time;
Howdy compare 1.91-1.97 s, with 2 of 6 runs timing out at 5.8 s. Both accept
the laptop looking up at the face (native scored 1.24 straight on, 3.11 at an
angle; 6.0 passes). About 1 s of every scan is the camera producing its first
frame (raw V4L2 measures the same), so it is the floor for either engine. The
earlier impression that native was slower came from a setup where PAM was
still using Howdy.

- [x] Add a timing breakdown to `faceunlock test`: time to the first lit frame,
      frames with a detected face, time to a match
- [x] Compare both: native was faster and never failed in the runs above;
      it needs no AUR packages, which suits KDE upstream
- [x] Merge the chosen branch into `master`; keep the other as an archive branch
- [ ] Shave the remaining time: open the camera and load the networks in
      parallel, and check whether opening the camera (~330 ms) can be faster

## 2. Warning labels for risky options

Show a short warning (Kirigami.InlineMessage or a ContextualHelpButton) where an
option trades security or privacy for convenience:

- [x] Administrator prompts and sudo: your face can approve changes to the system
- [x] Match threshold set lenient: people who look like you may get in
- [x] "Don't use when the lid is closed" or "Don't use over SSH" turned off
- [x] Dismiss the lock screen automatically: unlocks as soon as you are seen,
      without a key press
- [x] Take a photo of failed attempts: hidden instead, since the native engine
      doesn't take photos yet (see Follow-ups); warn when it is implemented
- [x] "Say when face recognition starts" turned off: the login and lock screen
      animation depends on it

## 3. Laptops with only an ordinary (colour) webcam

Setup used to require an IR camera. Without one, it now offers the ordinary
webcam:

- [x] Setup finds the colour webcam when there is no IR camera and explains,
      with a warning to acknowledge, that it is less accurate, can be fooled by
      a photo of you and needs your face lit from the front. Virtual cameras
      (v4l2loopback) are never picked: any program can feed them a video
- [x] In that mode, default to the lock and login screens only; sudo and
      administrator prompts stay on the password (turning them on shows a red
      warning)
- [x] Try both approaches and keep the more reliable one. Measured on
      2026-10-30 with the Asus Vivobook 16's 720p webcam, enrolled and tested in
      the same light, scores (lower is better, 6 passes):
  - mimic IR (greyscale + CLAHE): 0.40-0.70
  - colour as-is: 0.16-0.20, and the fastest  ← chosen (`video/colour = true`)
  - colour + CLAHE: 0.13-0.55, 80 ms slower
  In dim light, against a face enrolled with the lights on, none of them found
  a face; lowering the detector threshold to 0.4 found one in 1-2 of ~100
  frames, scoring 6.9-10.8 (rejected). With a bright window behind the user no
  face was found either. Lighting is the limit, not the preprocessing.
- [ ] Later: a liveness check (blink or a head turn) to make photos useless

## Follow-ups (not yet ordered)

- [x] Plug and play: install/upgrade drops pam_howdy lines once Howdy is gone,
      removal drops pam_faceunlock lines, and the first login after startup
      (uptime under 10 minutes) offers Set Up once if there is an IR camera
- [ ] native engine: "Take a photo of failed attempts" isn't implemented; the
      option is hidden in System Settings until it is (show a privacy warning then)
- [ ] A root service that owns the camera and face models (like fprintd), so
      models no longer have to be readable by local users
- [ ] Upstream route: a `kde-face` PAM service and a face authenticator in
      kscreenlocker beside fingerprint; the login manager auto-scan as a merge
      request; the settings page as a KCM or part of Users
- [ ] A trademark-safe product name before any commercial release
