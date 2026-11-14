# Spite's Linux download

The scripts here build `Spite-linux-x86_64.tar.xz`, the ready-to-run download that the main README's [Install](../../README.md#install) section links to. It holds Spite, PgD4S's two LV2 plugins, the Max for Live converter, and an installer that needs no root access.

## Make it and test it

You need Docker and about 4 GB of free disk space.

```sh
tools/linux_download/make-download.sh     # builds the public repository's master
tools/linux_download/test-download.sh     # installs and checks it on five clean systems
```

`make-download.sh` clones Spite from GitHub with every submodule, so it builds what users get, not your working copy. Give it a branch, a tag or a commit to build something else. The first run downloads the sources and compiles for about 15 minutes with 12 jobs. Later runs only rebuild what changed, and `make-download.sh --pack` packs the last build again without compiling.

Everything it makes goes to `~/.cache/spite-download`. Set `SPITE_DOWNLOAD_WORK` to use another folder.

| folder | what's in it |
| --- | --- |
| `src/Spite` | the clone |
| `build`, `stage`, `deps` | PgD4S's build, Spite's installed files, and aubio |
| `out` | the download, its `.sha256`, `release-notes.md` and `release.env` |
| `tests/<image>` | each test's logs and a screenshot |

`test-download.sh` starts a clean container of Ubuntu 24.04, Ubuntu 26.04, Debian 13, Fedora and Arch. In each one, as a normal user, it:

1. extracts the download and runs `install.sh`;
2. checks that every program and library in it finds the libraries it needs;
3. makes a session without a screen, loads PgD4S and PgD4S-fx on tracks, runs it for 3 seconds and saves it;
4. runs the Max for Live converter;
5. starts Spite on a virtual screen and takes a screenshot;
6. checks that `install.sh` and `uninstall.sh` refuse to run while Spite is open;
7. installs again over the first install, as an update;
8. checks that `install.sh` won't replace a Spite built from source unless it's given `--force`;
9. runs `uninstall.sh`.

The containers have no sound card and no graphics card. Sound, and PgD4S's windows on a real graphics card, need a test on a real desktop.

## Publish it

Only publish a download that passed `test-download.sh`. The file `out/release.env` has the version, the tag and the commit the download was built from:

```sh
cd ~/.cache/spite-download/out && . ./release.env
gh release create "$TAG" --repo topemalheiro/Spite --target "$COMMIT" --title "Spite $VERSION for Linux" \
    --notes-file release-notes.md Spite-linux-x86_64.tar.xz Spite-linux-x86_64.tar.xz.sha256
```

The file has the same name in every release, so `https://github.com/topemalheiro/Spite/releases/latest/download/Spite-linux-x86_64.tar.xz` always gives the newest one.

**Never make a release tag with `git tag -a`.** Spite takes its version from `git describe`, which reads annotated tags. An annotated tag called `v9.8.417` would turn the next build's version into "v9" and break its file names. `gh release create` makes a plain (lightweight) tag, which `git describe` ignores.

## What's in the download

| path | what it is |
| --- | --- |
| `install.sh`, `uninstall.sh` | install into `~/.local/opt/spite9` for one user, and remove it again |
| `bin/spite9` | the launcher, which starts Spite from wherever the folder is |
| `bin/spite9-lua`, `bin/spite9-export` and the other `bin/spite9-*` | Spite's command-line tools |
| `bin/spite9-m4l2pd` | the Max for Live converter, `tools/m4l/m4l2pd.py` |
| `lib/` | Spite's program, its own libraries, control surfaces, panners and audio backends |
| `lib/LV2/` | the LV2 plugins Spite ships with, plus `PgD4S.lv2` and `PgD4S-fx.lv2` |
| `lib/fallback/<library>/` | system libraries, each used only where the user's system doesn't have it |
| `share/`, `etc/` | Spite's data and default settings |
| `share/doc/` | licences, the converter's guide, and where each bundled library comes from |

## Why it's built this way

- **It's built on Ubuntu 24.04.** A program built on one Linux runs on systems with the same C library or a newer one, so the build uses the oldest system the download supports. Ubuntu 24.04 is also the oldest Ubuntu that plugdata's own automated builds use.
- **The launcher runs Spite in Ardour's "bundled" mode** (`ARDOUR_BUNDLED`), as Ardour's own downloads do. The program then finds its files next to itself, instead of at the path it was configured with.
- **Libraries every desktop has come from the user's system:** the C and C++ libraries, graphics, X11, the sound servers, glib, cairo, pango, fonts and curl. They have to match the user's drivers and servers, and their newer versions stay compatible. The list is `HOST` in `container/bundle-libs.py`.
- **The other libraries are in the download, each in its own folder under `lib/fallback/`.** The launcher adds a folder to the library path only when the system doesn't have that library (`files/paths.sh`). Putting them all first on the path broke Debian 13 and Ubuntu 26.04: their GnuTLS needs a newer nettle than Ubuntu 24.04's, and it got the bundled one.
- **Only the libraries that Spite and PgD4S name themselves are followed.** The script doesn't look inside system libraries for more to copy. Otherwise curl's Kerberos and LDAP libraries would come along, in versions older than the system's.
- **aubio is built here, without FFmpeg** (`container/build-aubio.sh`). Ubuntu's aubio links FFmpeg, which would add dozens of libraries. It's the one library that always comes from the download.
- **Spite is configured with `--no-lrdf`.** Ubuntu's lrdf needs a libcurl that only Debian and Ubuntu have. Without lrdf, LADSPA plugins get no categories or presets from RDF files, and sound files can't be tagged in the import dialog. Plugins load the same.
- **`--libjack=weak`.** Spite then starts on systems without JACK, and loads JACK only when it's there.
- **`--no-phone-home`.** Otherwise Spite asks ardour.org for news when it starts, and shows Ardour's update notices.
- **The launcher sets `LV2_PATH` when it's unset.** Ubuntu's LV2 library only knows Debian's plugin folders, so the launcher adds the ones Fedora and Arch use, `/usr/lib64/lv2` and `/usr/lib/lv2`.
