# reloaded

[По-русски](README.ru.md)

Takes out of «Петька 2: Судный день» Reloaded (Steam, 2016) the pictures the
original game lacks, and puts them in the game's folder as `RELOADED.STR`,
the game's own kind of store. With the extras menu's restored dialogues on,
the launcher shows them where the script asks for a clip that the game's
resource tables do not have.

```
pip install UnityPy Pillow
python3 reloaded.py <Reloaded's folder> <the game's folder>
```

Reloaded's folder is the one with `petka2_Data`. Nothing of Reloaded is in
this repository: the store is made from your own copy.

What it takes:

- the apiary's beekeeper dead drunk (Reloaded's `DEDVSE`, sprite
  `S-T-22__001`): once he has given the glasses away the script shows him
  in place of the sober one, and the talk is of how quickly he got there,
  but the game has no picture of him (no resource 1111), and he was gone.

For the Switch, `RELOADED.STR` goes beside the game's other files in
`sdmc:/switch/petka2/`; `PETKA_GAME=2 make nsp-full` takes it if it is in
the folder `PETKA_ASSETS` names.
