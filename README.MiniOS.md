# MiniOS Xfce settings development fork

Base: Debian Trixie `xfce4-settings 4.20.1-1`, downloaded with `apt-get source`
inside the persistent Trixie development container. The Debian packaging and
upstream GPL licensing are retained. Original packaging repository:
https://salsa.debian.org/xfce-team/desktop/xfce4-settings

Original source checksums (SHA-256):

* `.dsc`: `3e11a07580f1566e6ac2fb463311373f27e316a78b328d6608811c2435fe2938`
* `.orig.tar.bz2`: `fd0d602853ea75d94024e5baae2d2bf5ca8f8aa4dad7bfd5d08f9ff8afee77b2`
* `.debian.tar.xz`: `e04172542edd52843a0a8f5ad8958197750b18c1c8703a3fdd265b2cb43de28b`

The package retains the `xfce4-settings` / `xfce4-helpers` names and executable
paths. MiniOS builds require `4.20.1-1minios3` for Trixie, so an image
cannot silently substitute an unpatched settings daemon. The candidate must
be exposed to the build's APT repository before rebuilding the image. This work
does not publish a repository or create a remote Git fork automatically.

The maintained changes are in the display dialog, X11 display backend,
XSettings serializer, appearance dialog, and shared RandR cache. Debian source
generation records the upstream delta in a single quilt patch. The Xsession
hook enables Qt5's native high-DPI support without setting a frozen scale value.
No KDE or GNOME session components are required.

Interface scale is selected in the existing Display settings grid, below output
pixel scale. The original Apply button previews both controls with one timed
confirmation. Combined changes restore the monitor profile and interface scale
together on cancellation or timeout. The dialog retains upstream natural sizing.

The system `displays.xml` sets Automatic (`GlobalScale=0`) as the default.
Saved user percentages override it. Resetting the preference reveals Automatic
again; no package script rewrites an existing user's explicit selection.

Preview rollback restores a complete live CRTC snapshot, including primary
output and the previous managed/unmanaged scale, in one server configuration
pass. RandR geometry and its compensation atom are published as completed
generations under the same server grab. Readers retain their previous cache
until completion and refresh before saving a combined confirmation.

When no global scale has been selected, the scale combo shows the existing
**Default** label for preserved legacy settings. Selecting **100%** explicitly
starts a preview even in this state. See [transaction implementation and
regression checks](docs/scale-transactions.md) for the protocol and test coverage.

Architecture, automatic-detection policy, driver restrictions, acceptance
evidence and outstanding work are documented in the owning workspace's
`docs/xfce-hidpi.md`. Toolkit probes are acceptance fixtures, not production
package dependencies. Build their binaries in the Trixie development container
as `builder`; Testo consumes them in a disposable MiniOS X11 guest.

The local Git baseline is Debian's `debian/4.20.1-1` tag, commit
`578d861942e1849465efb57bf298a9c035b223fc`; all MiniOS changes remain
uncommitted above it. The workspace registers this directory as a submodule
using the original Debian repository URL. Remote fork creation and publication
remain maintainer release actions.

Localization configuration and lock state are maintained in the parent
`minios-live` workspace. Run `lokit status --target xfce4-settings-minios` from
that workspace root; the PO catalogs remain in this repository.
