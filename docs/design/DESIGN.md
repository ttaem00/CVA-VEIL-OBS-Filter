# VEIL product design contract

Outcome: a broadcaster carries their exact saved camera look into OBS, then
adjusts face details without leaving OBS or operating a browser/server.

Use OBS's native, theme-aware properties, keyboard navigation, numeric slider
inputs, colour picker and file picker. Keep one visible editing section at a
time: Quick, Skin, Face details, Makeup, Light/colour, Preset files, Diagnostics,
Background removal.
Quick opens with overall strength and output view. The comparison position
appears only in split view. Presets and additional options follow in separate
groups. The extra controls default to zero; no automatic retuning of the saved
21 values.

The Quick layout draws on three visually inspected references.

- Show the selected output mode clearly and put a numeric value beside the
  slider. [Elgato Camera Hub settings](https://help.elgato.com/hc/en-us/articles/4405055113357-Elgato-Facecam-Camera-Hub-Settings-Overview#h_01G8EYHB03TVGQ4VRQ9CPDZKRQ)
- Keep the OBS preview prominent and line up secondary controls in compact
  groups. [NVIDIA Broadcast camera panel](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-app/)
- Keep the active mode and return action in view as the user opens other
  settings. [Canva recording flow on Mobbin](https://mobbin.com/explore/flows/3400f6b8-9a95-458b-91e6-9b5efa69b5e2)

Face detail labels state the direction of signed controls. Zero keeps the
current face. All settings persist in the OBS filter. Old browser JSON imports
all 21 values, two colours and mirror exactly; VEIL JSON also saves additions.
Import validates everything before mutation. An invalid file keeps the look.
Restore is discoverable and restores the original personal preset.

Natural shape is enabled by default, keeps the stored values, blends overlapping
deformations as one bounded field and fades them with face presence. The checkbox
in Quick allows the old shape style. Background mode is Off/automatic RVM/
Chroma. The default product supports automatic person removal only; reference
registration is archived behind VEIL_REGISTERED_BACKGROUND=OFF. Do not expose
mode/registration/preview/deletion controls, load old reference pixels or touch
stored reference files. Legacy mode3 maps to automatic1 on load/import/export.
Each processing filter owns its live settings: person has stability, local device,
status/restart; edge has fractional feather0..50; chroma has key colour/similarity/
smoothness/spill. Main beauty has no duplicate background sliders or Apply, only
a location guide and create-missing button. Native OBS updates apply each edit.
Creation preserves existing values/eyes/order/retry and person UUID. Ownership
uses marker + actual same-parent beauty UUID, never names. Orphan/independent
filters stay editable. OBS eyes remain native; person and chroma are alternative
background methods. Preserve the unchanged upstream finishing C/shaders.
Preset export snapshots actual children; background-bearing v3+ imports apply
once, face-only/builtin presets preserve backgrounds. Device choice stays local.
v5 supports fractional feather and v1-v4 import. Disabling beauty alone does not
disable separate background filters. The retained reference feature source is
on the separate `archive/registered-background` branch, not a supported product mode.

Show tracker preparation, detected face, waiting for a face and initialization
failure in plain Korean. A refresh button updates the status without reopening
the camera. The native filter consumes OBS frames and never opens another camera.
Camera selection/resolution, recording, screenshots and final output use the
existing OBS controls; document their mapping instead of duplicating them.

Original/split/mask modes affect actual output. The warning and a button to
return to processed output stay visible when switching sections. The mask
option is also marked in the selector. No hidden auto-save of images or network
upload.

Installer: one primary install/update action, secondary reversible rollback,
and an always available close/cancel. Show readable Korean, installed location,
working/success/failure status, and actionable OBS-running errors. Preserve all
other plugins and OBS configuration. The installer never forcibly exits OBS.

Verification: real OBS property callbacks and GPU pixels are required for
behavior. Visible OBS UI and camera output remain a separate acceptance check.
An isolated test or mock screen cannot establish actual camera/UI success.
Latency improvements preserve model, resolution, recurrence ratio and all quality
math. Exact SIMD/scalar conversion and postprocess equivalence precedes timed
comparison. Separate model/CPU timing from OBS capture/upload and actual camera
latency; never improve a reported number by moving its timestamp boundary.

User copy: no model names (RVM/BMv2) or frame-queue counters in normal settings.
Use automatic background removal / remove using registered background / chroma.
The default product exposes no reference-photo controls. Archived feature code
and its generated-fixture tests are retained separately from current acceptance.

Clarity principle (user2026-11-03): the best interface is understood at a glance
without explanatory prose and is visually pleasant. Usability/accessibility come
first. Establish importance through size, contrast, grouping, layout and space.
Native OBS: keep the current category/actual controls primary; use optional
long-description tooltips for detail, show warnings only when relevant, and show
action results only after an action. No standing instructional paragraphs.
Preserve native theme colours, keyboard/numeric input and meaningful labels.
Installer: large brand, one short purpose, compact status card, one install/update
CTA, subordinate rollback/close. Put path/error details behind an accessible
expander; high-contrast/system colours and responsive keyboard flow are required.
A working operation must leave the UI responsive and finish atomic file changes
before a queued close. Do not fake progress percentages.
