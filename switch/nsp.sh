#!/bin/sh
# nsp.sh -- petka.nsp: the game as an installed title on the Switch, with its
# icon on the home menu, from the petka.elf that `make` builds.
#
#   make nsp                     (or ./nsp.sh after make)
#   PETKA_ASSETS=<game folder> make nsp-full     (or ./nsp.sh full)
#
# nsp-full makes petka-full.nsp, with the game's files inside it (its
# romfs): those assets.txt lists, the ones the launcher reads, taken from
# PETKA_ASSETS -- the game's folder, as the desktop launcher wants it. The
# saved games and the settings still go to sdmc:/switch/petka/.
#
# Needs hacBrewPack, built into deps/ (see README), and the console's own
# keys: prod.keys, dumped from it with Lockpick_RCM, at ~/.switch/prod.keys,
# at .switch/prod.keys in the repository, or where PETKA_KEYS says. Its NCAs
# are signed with no key Nintendo has, so the console runs them only with
# signature patches.
#
# RU: petka.nsp -- игра как установленное приложение с иконкой на главном
# экране, из petka.elf, который собирает make. Нужны hacBrewPack (в deps/) и
# ключи консоли prod.keys (снимаются Lockpick_RCM) в ~/.switch/prod.keys, в
# .switch/prod.keys в корне репозитория или по пути из PETKA_KEYS; на
# консоли -- патчи подписей. nsp-full кладёт внутрь файлы игры из списка
# assets.txt, взятые из папки PETKA_ASSETS.
set -e
FULL=${1:-}
cd "$(dirname "$0")"

# The title's id: an application's, unique to this game (the low 16 bits
# are an application's 0000); the same in petka.json.
TITLE_ID=0x0100F35700A50000
# The keys: PETKA_KEYS, else ~/.switch/prod.keys, else .switch/prod.keys at
# the top of the repository (which .gitignore keeps out of it).
KEYS=${PETKA_KEYS:-$HOME/.switch/prod.keys}
[ -n "$PETKA_KEYS" ] || [ -f "$KEYS" ] || KEYS=../.switch/prod.keys
HBP=deps/hacbrewpack/hacbrewpack
TOOLS=${DEVKITPRO:-/opt/devkitpro}/tools/bin
# The NACP's name, author and version: the Makefile's (make nsp passes them).
TITLE=${APP_TITLE:-Петька и Василий Иванович спасают галактику}
AUTHOR=${APP_AUTHOR:-Бука}
VERSION=${APP_VERSION:-0.1.0}

[ -x "$HBP" ] || { echo "no $HBP: build hacBrewPack into deps/ first (see README)"; exit 1; }
[ -f "$KEYS" ] || { echo "no keys at $KEYS: dump the console's prod.keys with Lockpick_RCM (or set PETKA_KEYS)"; exit 1; }
[ -f petka.elf ] || { echo "no petka.elf: run make first"; exit 1; }
if [ "$FULL" = full ]; then
    [ -n "$PETKA_ASSETS" ] && [ -d "$PETKA_ASSETS" ] ||
        { echo "nsp-full: set PETKA_ASSETS to the game's folder (the one the desktop launcher runs from)"; exit 1; }
fi

W=build/nsp
rm -rf "$W"
mkdir -p "$W/exefs" "$W/control"
# The program: the ELF as an NSO, and the process it runs as (petka.json).
"$TOOLS/elf2nso" petka.elf "$W/exefs/main"
"$TOOLS/npdmtool" petka.json "$W/exefs/main.npdm" 2> /dev/null
# Its name and icon on the home menu, the same in every language.
"$TOOLS/nacptool" --create "$TITLE" "$AUTHOR" "$VERSION" "$W/control/control.nacp" --titleid=$TITLE_ID
for lang in AmericanEnglish BritishEnglish Japanese French German LatinAmericanSpanish Spanish Italian Dutch \
            CanadianFrench Portuguese Russian Korean TraditionalChinese SimplifiedChinese BrazilianPortuguese; do
    cp icon.jpg "$W/control/icon_$lang.dat"
done
ROMFS=--noromfs
OUT=petka.nsp
if [ "$FULL" = full ]; then
    # The game inside: each file of assets.txt, found whatever its case in
    # the places the launcher looks, under the name the launcher asks for.
    # Hard links where they can be (no copy of 2 GB), copies where not.
    mkdir -p "$W/romfs"
    missing=0
    grep -v '^#' assets.txt | grep -v '^ *$' | while read -r name; do
        src=""
        for dir in . data1_out/Main data1_out/Flics data1_out/Sfxs Main Flics Sfxs; do
            [ -d "$PETKA_ASSETS/$dir" ] || continue
            src=$(find "$PETKA_ASSETS/$dir" -maxdepth 1 -type f -iname "$name" | head -1)
            [ -n "$src" ] && break
        done
        if [ -z "$src" ]; then echo "nsp-full: $name is not in $PETKA_ASSETS"; echo x >> "$W/missing"; continue; fi
        ln "$src" "$W/romfs/$name" 2> /dev/null || cp "$src" "$W/romfs/$name"
    done
    [ ! -f "$W/missing" ] || { echo "nsp-full: files missing, nothing built"; exit 1; }
    echo "romfs: $(ls "$W/romfs" | wc -l | tr -d ' ') files, $(du -sh "$W/romfs" | cut -f1)"
    ROMFS="--romfsdir $W/romfs"
    OUT=petka-full.nsp
fi
"$HBP" -k "$KEYS" --titleid "${TITLE_ID#0x}" --exefsdir "$W/exefs" --controldir "$W/control" $ROMFS --nologo \
    --nspdir "$W/out" --ncadir "$W/nca" --tempdir "$W/temp" --backupdir "$W/backup"
mv "$W"/out/*.nsp "$OUT"
# The built title takes as much room again: the romfs goes.
rm -rf "$W"
echo "built ... $OUT"
