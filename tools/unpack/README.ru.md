# ptk-unpack

[English](README.md)

Достаёт файлы игры из образов её дисков — на Windows, Linux и macOS, без
каких-либо установленных программ: один файл на Go, только стандартная
библиотека.

Цепочка — `.isz -> .iso -> файлы`:

- образ ISZ (сжатый ISO от UltraISO: блоки zlib/bzip2, в том числе
  многотомные образы) читается как ISO, который в нём лежит, блок за блоком —
  промежуточный ISO на диск не пишется;
- читается файловая система ISO 9660, с именами Joliet, если они есть;
- архив InstallShield среди файлов (`data1.cab`, где установщик хранит
  программу и основные хранилища) распаковывается так же, как это делает
  `unshield`: `<группа файлов>/<папка>/<файл>`.

```
ptk-unpack game <папка> <образ>...        готовая папка игры со всех дисков
ptk-unpack extract <образ> <папка>        все файлы одного образа
ptk-unpack isz2iso <образ.isz> [out.iso]  ISO из образа ISZ, со сверкой CRC
ptk-unpack cab <data1.cab> <папка>        распаковка архива InstallShield
ptk-unpack ls <образ>                     список файлов образа
```

Для «Петька и Василий Иванович спасают галактику»:

```
ptk-unpack game Petka "…CD 1.isz" "…CD 2.isz" "…CD 3.isz"
```

даёт папку, в которой работает лаунчер: хранилища `.STR`, `intro.avi`,
`disk1.id`..`disk3.id` и `data1_out/` (`Main`, `Flics`, `Sfxs`). Лаунчер
кладётся рядом с ними. Сверено с папкой, собранной вручную с тех же дисков:
все 31 файла совпадают побайтно.

## Сборка

```
go build -o ptk-unpack .
```

Сразу под все системы (статические бинарники, без cgo):

```
for t in darwin/arm64 darwin/amd64 linux/amd64 linux/arm64 windows/amd64 windows/386; do
  os=${t%/*}; arch=${t#*/}; ext=; [ $os = windows ] && ext=.exe
  CGO_ENABLED=0 GOOS=$os GOARCH=$arch go build -trimpath -ldflags "-s -w" -o dist/ptk-unpack-$os-$arch$ext .
done
```

## Ограничения

- зашифрованные или защищённые паролем образы ISZ не поддерживаются;
- архив InstallShield должен содержать свой дескриптор (без отдельного
  `data1.hdr`) и лежать в одном томе; файлы, разбитые на `data2.cab` и
  далее, не распаковываются;
- форматы описаны по ISZ-tool (Olivier Serres) и unshield; код ни из одного
  из них не взят.
