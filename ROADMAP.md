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

- [x] Add a timing breakdown to `kenning test`: time to the first lit frame,
      frames with a detected face, time to a match
- [x] Compare both: native was faster and never failed in the runs above;
      it needs no AUR packages, which suits KDE upstream
- [x] Merge the chosen branch into `master`; keep the other as an archive branch
- [x] Shave the remaining time: the networks load while the camera opens and
      stay loaded in the service. That takes ~50 ms of work out of each scan, but
      scans vary by about as much from run to run (1.50-1.64 s). What is left is the
      camera: ~220 ms to wake it from USB suspend (format hints to OpenCV don't
      help) and ~1 s until the IR sensor delivers its first frame. Keeping the
      camera awake would cost battery all day, so it stays as it is

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

- [x] Packages for Debian/Ubuntu and Fedora beside Arch, from one CMake install,
      built and install-tested in containers (Ubuntu 26.04 and 26.10, Fedora 44);
      the lock screen add-on keeps its own copies of the originals instead of
      using pacman's cache
- [ ] Lock screen animation on Plasma 6.3-6.7 (Debian 13, Ubuntu 26.04 LTS): those
      versions need their own patch, tested against a running Plasma
- [ ] Login screen face unlock beyond Arch: plasma-login-manager-kenning for
      Fedora, or an SDDM greeter patch
- [x] Plug and play: install/upgrade drops pam_howdy lines once Howdy is gone,
      removal drops pam_kenning lines, and the first login after startup
      (uptime under 10 minutes) offers Set Up once if there is an IR camera
- [ ] native engine: "Take a photo of failed attempts" isn't implemented; the
      option is hidden in System Settings until it is (show a privacy warning then)
- [x] A root service that owns the camera and face models (like fprintd), so
      models no longer have to be readable by local users: `kenning daemon`,
      socket-activated, answering only for root or the user's own account
- [x] Use Plasma 6.8's own Face sign-in method (PAM service `kde-face`,
      `[Authenticators] Face=true`, which had no backend): pam-tidy writes
      `kde-face` from the distribution's `kde-fingerprint`, failing closed (a
      `pam_deny.so` stands behind `pam_kenning.so`, since kde-fingerprint's
      stack without its module line lets anyone in), and moves between it and
      the `kde` service when Plasma is upgraded or downgraded. Face is switched
      on per user (System Settings, or at login for users with a face model).
      Lock screen add-on patch 7 starts with Face, falls back to the password on
      a failure instead of Plasma's immediate retry, and switches to the
      password when you type
- [ ] Upstream route, still to do: the login manager auto-scan as a merge
      request, the settings page as a KCM or part of Users
- [ ] kscreenlocker fixes to send: (1) a remembered method that has since been
      switched off crashes the lock screen until KWin gives up ("loginctl"
      screen): bug 528042, merge request !387 with an autotest. Kenning never
      switches Face off without forgetting a remembered Face.
      (2) a method started before its PAM worker is ready isn't retried until
      a key or mouse wakes the screen (the add-on retries for fingerprint;
      being fixed in !352).
      (3) a real preferred-method setting, and starting fingerprint only after
      face fails, so the add-on's workarounds can go
- [x] Preferred sign-in method when a fingerprint is enrolled (System Settings,
      Lock screen): face first (fingerprint and password after a face failure),
      or fingerprint first (face and password when it fails or times out), or
      the method used last. Fingerprint runs alongside face from the start
      until (3) above lands
- [x] A trademark-safe product name: **Kenning** (to ken = to know, recognise),
      in KDE's style of real words with a K. Quick web checks found no clash in
      face recognition or authentication; KFace (KDE's archived libkface),
      KVision (Kotlin framework), KGlance (Glance face unlock) and Kognize (close
      to the KOGNIZA trademark application) were ruled out. A formal trademark
      search (USPTO, EUIPO) is still needed before a commercial release
