# Display-scale transactions

These changes belong to the pending `4.20.1-1minios1` Debian entry above
`578d861942e1849465efb57bf298a9c035b223fc`. They correct the existing global
scaling implementation without introducing a new release number.

## Transaction ownership and rollback

The display dialog sets `/GlobalScale/RestoreProfile` before submitting `Temp`
for a combined output/scale preview. The backend captures every CRTC's mode,
rotation, dimensions, origin, transforms and output assignments, plus primary
output, compensation, effective scale, managed/unmanaged state and active-profile
metadata. A 15-second timeout is armed at this point, covering a frontend that
exits between submitting `Temp` and sending `/GlobalScale/Request`.

`Temp` is loaded into a separate in-memory CRTC layout. The live cache, primary
output, active-profile metadata and X server remain unchanged until Request
provides the final scale. A RandR reload between the two signals does not discard
the staged layout. The final layout and scale are validated and applied together;
for example, two 1920-wide outputs at 125% can preview two 3840-wide outputs at
200% on an 8192-wide framebuffer limit without attempting a 12288-wide
intermediate framebuffer. If the frontend exits before Request, the timeout drops
the staged layout without changing X or reverting unrelated live hotplug changes.

The Request starts the scale preview and replaces the staging timeout. Cancel,
timeout, apply failure and orderly daemon disposal restore the complete captured
configuration. The previous scale is never first applied to the temporary
layout. This matters when the old scale on a larger temporary layout would
exceed the framebuffer limit even though the actual rollback layout fits.

The frontend no longer requests a separate `Default` profile application on
combined cancellation. The backend restores the snapshot and clears `Temp`.
Confirmation refreshes the dialog's live RandR cache before sending Confirm.
After accepting it and revalidating Automatic, the backend refreshes its own
coherent live RandR cache and saves `Default`, persists the scale choice, clears
the snapshot and sets active-profile metadata to `Default` for a combined
transaction. The frontend never overwrites the persistent profile before the
backend accepts confirmation.

If restoring the snapshot itself fails, `/GlobalScale/Error` is
`rollback-failed`; the dialog does not claim restoration succeeded. A disconnected
monitor, lost mode or driver failure can make a formerly valid snapshot
unavailable. No implementation can guarantee restoration of removed hardware.

## Coherent geometry generations

`_XFCE_GLOBAL_SCALE_COMPENSATION` remains a single CARDINAL holding a 16.16
fixed-point compensation. `_XFCE_GLOBAL_SCALE_GENERATION` is a CARDINAL sequence:

- Odd means configuration is in progress or failed before completion.
- Even means a completed geometry/compensation pair is available.
- An absent generation is accepted for compatibility with the original daemon.

The writer grabs the X server, publishes an odd generation, applies framebuffer,
CRTC and primary-output changes, and checks errors with same-connection Xlib
operations. On success it publishes compensation and the even generation before
ungrabbing. `UngrabServer` is flushed before `gdk_display_sync`, retaining the
fix for toolkit screen-change handlers which query X on another connection.
Failed configuration leaves an odd generation until successful restoration.

The shared RandR reader grabs the server across generation checking, resource
retrieval, compensation retrieval and CRTC/output queries. It performs no toolkit
or cross-connection synchronization while grabbed. A reload encountering an odd
generation returns false and retains the old cache. A new reader reports a
configuration-in-progress error instead of constructing a mixed cache.

The display dialog observes completion-property notifications as well as RandR
notifications, and refreshes once more before saving a confirmed combined
configuration. Toolkit-only scale changes also publish a completion generation.

## Profile and coordinate rules

- Profiles continue to store uncompensated output geometry.
- Startup reads the live compensation before the parent restores a profile.
  Profile loading uses that same compensated space from the beginning; outputs
  missing from a partial profile retain their correct live transforms/origins.
- Primary-output changes are tracked independently of CRTC dirty flags.
- Automatic selects the intended primary among active cached CRTC/output
  assignments, not stale `XRROutputInfo.crtc` associations. Disabled CRTCs are
  ineligible. Automatic is re-evaluated on confirmation after topology changes.
- Scale application constructs rounded, fixed-point target transforms and
  coordinates in a separate cache, subtracts minimum active origins, and
  validates those final extents before replacing the live cache or changing X.
- Canonical uncompensated output transforms are composed with each target
  compensation rather than repeatedly scaling a previously quantized transform.
  Each completed generation also publishes `_XFCE_GLOBAL_SCALE_CRTC_<id>`
  metadata containing the exact canonical double bits and a proof of the full
  live matrix, output set and compensation. Both the backend cache reload and
  shared RandR reader preserve canonical transforms while this proof matches.
  The shared reader therefore supplies exact canonical values on profile commit.
  Mode-only changes still read the new geometry live and retain canonical scale
  when the matrix/output set is unchanged. Genuine external matrix or assignment
  changes invalidate the proof and fall back to the observed server transform;
  no near-identity rounding heuristic hides even a one-unit external change.
- Absence of `/GlobalScale` is distinct from explicit 100%. The combo uses the
  existing `Default` string for the preserved legacy state; choosing 100% sends
  a request. Cancellation restores `ToolkitScale=0` and leaves legacy xfconf
  values intact.

The widgets remain ordinary GTK/libxfce4ui C widgets within Xfce's existing
dialog. No shared Python GUI implementation or application CSS is duplicated,
and no new GUI package dependency is needed.

## Regression checks

`debian/rules override_dh_auto_test` runs the original build-system check plus:

1. Five Automatic policy groups in `global-scale-policy.c`.
2. Eighteen transaction groups in `global-scale-transactions.c`, compiling the
   actual X11 backend with deterministic X/xfconf I/O substitutions. They cover
   a two-output rollback whose intermediate layout cannot fit, partial server
   failure and publication ordering, primary-only application, partial-profile
   startup, stale topology, positive/negative-origin normalization, unmanaged
   selection, frontend exit before Request, combined Cancel/Confirm, unmanaged
   rollback, failed rollback reporting, repeated fractional round trips and
   composition with an existing output pixel scale and preservation of the
   persistent profile when confirmation cannot read completed live state. The
   residual-fix groups exercise the production Temp-to-Request entry points,
   reload between those signals, frontend exit through the actual staging timer,
   50 Request/reload/Confirm cycles with exact 1.25 profile commits, geometry-only
   changes and invalidation on external matrices/output assignments.

`debian/rules check-global-scale-x11` is an explicit development gate requiring
`xvfb`, `xauth`, `dbus-run-session` and `xfconfd`. Its three real-X11 tests check that an incomplete generation
retains the existing cache/rejects new cache creation and that a reader on a
second connection waits for complete server-grab publication. The third test
executes the real shared reader and profile serializer against Xvfb and an
isolated xfconf daemon, including canonical double-bit portability and stale
matrix-proof rejection. Its XDG configuration/runtime directories are temporary
directories outside the source mirror. Xvfb does not
establish real multi-head driver or fractional-transform acceptance.

`debian/clean` removes all three generated executables. Build and run these
checks only in an external source mirror, as `builder` in the persistent Trixie
container. For the mirror used for this work, the inner commands are:

```sh
cd /build/dev/xfce4-settings-residuals/source
dpkg-checkbuilddeps
dpkg-buildpackage -b -us -uc >../ARCH.log 2>&1
debian/rules check-global-scale-x11 >../x11.log 2>&1
debian/rules clean >../clean.log 2>&1
test ! -e debian/global-scale-policy
test ! -e debian/global-scale-transactions
test ! -e debian/global-scale-generations
```

Development-container builds are not clean release validation. Actual
multi-monitor hotplug/lid behavior, GUI timing on real drivers, QXL restrictions,
image quality and full reboot persistence still need coordinator-approved
runtime acceptance. No VM or physical machine is modified by these checks.

### Initial development verification, 2026-10-09

Before the residual-fix review, both `trixie-amd64` and `trixie-i386` persistent
containers completed binary builds, generation checks and clean as their
unprivileged `builder` user with checks enabled. Each architecture
passed all 5 policy, 14 backend transaction and 2 X11 generation groups (21 total,
no skips). Debian clean succeeded, and explicit absence checks passed for all
three generated executables. Package metadata reported `4.20.1-1minios1` and the
expected architecture for both `xfce4-settings` and `xfce4-helpers`.

Host-visible artifacts, `.changes`, `.buildinfo` and logs are retained under:

```text
/build/minios-build-containers/trixie-amd64/build/dev/xfce4-settings-audit-fixes-run/
/build/minios-build-containers/trixie-i386/build/dev/xfce4-settings-audit-fixes-run/
```

The binary build logs are `amd64.log` / `i386.log`; the explicit X11 gate and
cleanup logs are `x11.log` and `clean.log` in each directory. The new round-trip
test initially exposed i386/x87 transform drift; canonical uncompensated output
transforms fixed it before these successful final runs. Existing upstream i386
settings-editor format warnings and the helpers' unused `${shlibs:Depends}`
warning are outside these transaction changes.

### Residual-fix verification, 2026-10-09

Both persistent Trixie architectures built `4.20.1-1minios1` as `builder` with
checks enabled after the two residual fixes. Each passed 18 backend transaction
groups, 5 Automatic policy groups and 3 real-X11/profile groups (26 total, no
skips). Debian clean and all three generated-executable absence checks passed.

The new backend checks are:

- `/transactions/production-temp-request-large-layout`: runs the actual Temp and
  Request handlers, including the production reload function between signals;
  applies the valid 7680-wide final configuration, cancels it and confirms it.
- `/transactions/large-temp-frontend-exit`: dispatches the real staging timeout
  source without Request and verifies zero CRTC configuration calls.
- `/transactions/reload-cycle-profile-commit`: runs 50 Request/reload/Confirm
  cycles through cache destruction and `list_crtcs`, preserving exact 1.25
  canonical factors and profile values, then checks the original framebuffer.
- `/transactions/external-transform-and-assignment`: observes a geometry-only
  mode change while retaining scale, rejects stale metadata on matrix/assignment
  changes, and preserves a genuine one-fixed-point-unit near-identity change.

`/generations/canonical-reader-profile-commit` executes the real shared reader
and `xfce_randr_save_output` with a private xfconf daemon. The backend's simulated
RandR I/O supplies fractional/multi-head transforms that Xvfb does not support;
this real-X11 gate independently covers metadata encoding, canonical reads,
profile serialization and stale-proof rejection.

Host-visible evidence and binary artifacts are retained separately from the
initial audit build:

```text
/build/minios-build-containers/trixie-amd64/build/dev/xfce4-settings-residuals/
/build/minios-build-containers/trixie-i386/build/dev/xfce4-settings-residuals/
```

The logs are `amd64.log` / `i386.log`, `x11.log` and `clean.log`. The optional X11
gate uses private temporary XDG configuration/runtime directories and a new
D-Bus session; it does not use the host desktop or existing xfconf settings.
The changelog, catalogs and root Lokit files were left unchanged.

## Localization handoff

PO/POT catalogs and root Lokit configuration are owned by the coordinator.
The initial implementation handed these two error messages to the coordinator:

- `A display configuration change is still in progress.`
- `The previous display configuration could not be restored.`

Both are necessary to distinguish an incomplete configuration or failed
restoration from the existing message which asserts successful restoration.
The residual fixes introduce no new UI strings and do not edit catalogs.
