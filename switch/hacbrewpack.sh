#!/bin/sh
# hacbrewpack.sh -- once: gets hacBrewPack 3.05 and builds it into deps/, for
# `make nsp`. The-4n's own repository is gone; this is a copy of it. Also the
# boot logo a title's Program NCA carries (deps/logo: NintendoLogo.png,
# StartupMovie.gif), as nton bundles it.
#
# RU: Один раз: берёт hacBrewPack 3.05 и собирает его в deps/ для make nsp.
# Исходный репозиторий The-4n удалён, это его копия. И логотип запуска для
# Program NCA (deps/logo), как его кладёт nton.
set -e
cd "$(dirname "$0")"
mkdir -p deps
[ -d deps/hacbrewpack ] || git clone --depth 1 https://github.com/rlaphoenix/hacBrewPack.git deps/hacbrewpack
mkdir -p deps/logo
for f in NintendoLogo.png StartupMovie.gif; do
    [ -s deps/logo/$f ] || curl -fsSL -o deps/logo/$f https://raw.githubusercontent.com/rlaphoenix/nton/HEAD/nton/assets/logo/$f
done
cd deps/hacbrewpack
[ -f config.mk ] || cp config.mk.template config.mk
make -j8
echo "built ... deps/hacbrewpack/hacbrewpack"
