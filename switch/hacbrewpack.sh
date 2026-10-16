#!/bin/sh
# hacbrewpack.sh -- once: gets hacBrewPack 3.05 and builds it into deps/, for
# `make nsp`. The-4n's own repository is gone; this is a copy of it.
#
# RU: Один раз: берёт hacBrewPack 3.05 и собирает его в deps/ для make nsp.
# Исходный репозиторий The-4n удалён, это его копия.
set -e
cd "$(dirname "$0")"
mkdir -p deps
[ -d deps/hacbrewpack ] || git clone --depth 1 https://github.com/rlaphoenix/hacBrewPack.git deps/hacbrewpack
cd deps/hacbrewpack
[ -f config.mk ] || cp config.mk.template config.mk
make -j8
echo "built ... deps/hacbrewpack/hacbrewpack"
