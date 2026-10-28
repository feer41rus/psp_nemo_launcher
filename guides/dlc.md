# Add-ons (DLC): install and delete

[Русская версия](dlc.ru.md)

Nemo Launcher (since 1.2.5) puts DLC into the game folders by itself, adapts DLC made for another version of a game, and remembers what it installed so you can delete the DLC you choose later. No DLC plugins are needed: ARK-5 serves decrypted DLC to games itself (built-in NoDRM).

## In short

1. Copy the DLC folders to the memory stick into **`PSP/NEMO/install_dlc/`** — as they are, no renaming.
2. In Nemo open the game card → **Triangle › Add-ons (DLC) › Install DLC**.
3. Confirm. Nemo moves the DLC into the game's folder and removes it from `install_dlc`.
4. The card shows **"DLC: installed"**. The list and deleting — **Triangle › Add-ons (DLC) › Installed DLC**.

## What goes into `install_dlc`

A usual DLC release looks like this — copy it like this:

```
PSP/NEMO/install_dlc/
├── Toy Story 3 - Alien Escapades (Europe) (DLC)/
│   └── ULES01406/
│       ├── PARAM.pbp
│       └── WORLD/ALIENS1.DPP.edat, ALIENS2.DPP.edat
├── Toy Story 3 - Buzz Adventures (Europe) (DLC)/
│   └── ULES01406/…
└── Toy Story 3 - Woody's Round Up (Europe) (DLC)/
    └── ULES01406/…
```

- A DLC pack is a folder with a **`PARAM.pbp`** (of the "add-on" kind). Nemo finds it at any depth.
- Nemo takes the DLC's name from its top folder: "Toy Story 3 - Buzz Adventures (Europe) (DLC)" → "Buzz Adventures". So better keep meaningful folder names.
- Several DLC for one game can go in at once — Nemo installs them all together.

## What Nemo does when installing

1. **Finds the DLC for this game.** A pack fits if it has the game's ID (e.g. `ULES01404`) or the same game title — then it is DLC for **another version** (region) of the game.
2. **Asks.**
   - Same ID: "Install DLC: 3?".
   - Another version: "DLC for another version of the game (ULES01406). Adapt it to this game (ULES01404)? It may not work". European DLC often works with another region's version of the same game, but not always — check in the game.
3. **Merges all packs** into the game's folder `PSP/GAME/<game ID>/` (a game looks for DLC only there, by its own ID). Identical files are not copied twice.
4. **When adapting**, writes your game version's ID into `PARAM.pbp`.
5. **Checks** that every file was copied whole, and **only then deletes** the pack from `install_dlc`. If copying stopped (e.g. the card is full), the pack stays — you can try again.
6. **Remembers** which files every DLC brought: `PSP/NEMO/dlc/<game ID>.txt`.

At the end — "DLC installed: N". The install is logged in `PSP/NEMO/nemo.log` (`dlc:` lines).

## Encrypted DLC

DLC is either **decrypted** (works on ARK-5 right away) or **encrypted**, as from the PlayStation Store (it needs a licence — a `.rif` file in `PSP/LICENSE/`).

- Encrypted DLC without a licence is **not installed**: "The DLC is encrypted — it will not work". It stays in `install_dlc`.
- DLC can only be decrypted with its original licence (e.g. with the NPdecrypter plugin on the console it was bought on). Nemo does not do that.

## Deleting

**Triangle › Add-ons (DLC) › Installed DLC**:

1. Mark the DLC with the select button (Cross or Circle — as in the XMB).
2. **Triangle — "Delete selected"** → "Delete DLC: N?" → "Yes".

Only the files of the chosen DLC go. The shared `PARAM.pbp` stays while other DLC remain. When the last one goes, Nemo removes the game's DLC folder too. The game's own files (e.g. `EBOOT.PBP` of PSN games living in the same folder) are never touched.

**DLC installed earlier by hand** (before Nemo 1.2.5 or by copying) shows as "DLC installed by hand" and cannot be deleted from Nemo: without a file list it is easy to wipe the game together with its DLC. Delete it from a computer, or install it again through `install_dlc` — then Nemo remembers it.

## If the DLC does not show in the game

- Is it the same game? DLC goes into **its** ID's folder — an ISO, a PSN copy and a disc of one game may have different IDs.
- DLC for another version may not fit — try DLC made for your region.
- Some games need a patch (game update) for their DLC — e.g. LittleBigPlanet.
- Do not enable NPloader and similar plugins with ARK-5: the built-in NoDRM already does their job.

## Thanks

- **ARK-5 / PRO CFW** — the built-in NoDRM (Stargate) that serves decrypted DLC to games.
- **RetroAchievements** — [the PSP DLC guide](https://docs.retroachievements.org/Working-with-DLC-for-the-PlayStation-Portable): where games look for DLC and why the version ID matters.
- **NPloader / NPdecrypter** (GameBrew) — how DLC used to be loaded.

The full credits are in the [README](../README.md#thanks).
