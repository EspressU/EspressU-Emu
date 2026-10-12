# EspressUTouchLab (vendored)

The on-screen control schemes offered under Settings > On-screen Controls > Control style
(Zone, Float, Adaptive, Frame, Racing). Vendored from EspressU-TouchLab at commit
`fbc9ba9` on its `feature/racing-mode` branch (not yet on its main), copied by hand with the
same steps as `tools/export-to-espressuemu.sh`, which refuses a commit that isn't on main. The
next export from main will carry these files once that branch is merged there.

Do not edit these files here. Change them in EspressU-TouchLab, run its checks, and
re-export - otherwise the next export silently overwrites the change.

Checks: `swift run --package-path src/ios/Packages/EspressUTouchLab touchlab-check`
