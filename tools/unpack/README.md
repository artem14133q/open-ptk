# ptk-unpack

[По-русски](README.ru.md)

Gets the game's files out of its CD images, on Windows, Linux and macOS,
with nothing else installed: one Go file, the standard library only.

The chain is `.isz -> .iso -> files`:

- an ISZ image (UltraISO's compressed ISO: zlib/bzip2 blocks, multi-part
  images too) is read as the ISO it holds, block by block -- no ISO is
  written on the way;
- the ISO 9660 file system is read, with the Joliet names where there are
  any;
- an InstallShield cabinet among the files (`data1.cab`, where the installer
  keeps the program and the main stores) is unpacked as `unshield` lays it
  out: `<file group>/<directory>/<file>`, with its descriptor read from
  `data1.hdr` beside it where the `.cab` has none of its own.

```
ptk-unpack game <out-dir> <image>...      a ready game folder from all the CDs
ptk-unpack extract <image> <out-dir>      every file of one image
ptk-unpack isz2iso <image.isz> [out.iso]  the ISO out of an ISZ image, checked
ptk-unpack cab <data1.cab> <out-dir>      unpack an InstallShield cabinet
                                          (and data1.hdr beside it, if any)
ptk-unpack ls <image>                     list the files of an image
```

For «Петька и Василий Иванович спасают галактику»:

```
ptk-unpack game Petka "…CD 1.isz" "…CD 2.isz" "…CD 3.isz"
```

gives the folder the launcher runs in: the `.STR` stores, `intro.avi`,
`disk1.id`..`disk3.id` and `data1_out/` (`Main`, `Flics`, `Sfxs`). Put the
launcher next to them. Checked against a folder made by hand from the same
discs: all 31 files identical.

For «Петька 2» (one disc, `data1.cab` with `data1.hdr`):

```
ptk-unpack game ptk2-files ptk2.isz
```

gives the `.STR` stores, `intro.avi`, `outro.avi`, `perelet.avi`,
`disk1.id`, `disk2.id` and `data1_out/Main` (`petka2.exe`, the publisher's
and developer's films, the credits). Checked against `unshield`: the
cabinet's 5 files identical.

## Building

```
go build -o ptk-unpack .
```

For every system at once (static binaries, no cgo):

```
for t in darwin/arm64 darwin/amd64 linux/amd64 linux/arm64 windows/amd64 windows/386; do
  os=${t%/*}; arch=${t#*/}; ext=; [ $os = windows ] && ext=.exe
  CGO_ENABLED=0 GOOS=$os GOARCH=$arch go build -trimpath -ldflags "-s -w" -o dist/ptk-unpack-$os-$arch$ext .
done
```

## Limits

- encrypted or password-protected ISZ images are not supported;
- an InstallShield cabinet must lie in one volume; files split across
  `data2.cab`... are refused;
- formats are after ISZ-tool (Olivier Serres) and unshield; no code is taken
  from either.
