# Moving from PROMOD (PRO-C) to ARK-5

[Русская версия](promod-to-ark5.ru.md)

For a PSP on **6.60 PROMOD-C2p** (or another PRO-C on 6.60/6.61) where PROMOD starts by itself on every boot. Result: official **6.61** with **ARK-5** booting by itself through cIPL. Tested on a PSP E1000 (Street).

While PROMOD stays active, FasterARK can't bring up ARK-5 (PROMOD hooks the reboot) and ARK cIPL Flasher crashes. So PROMOD goes first.

## What you need

- A fully charged battery, with the charger plugged in until the very end.
- On the memory stick:
  - the `PSP` folder from the `…_full_with_ark-5.zip` archive (or from the official [FasterARK](https://github.com/PSP-Arkfive/FasterARK) "full" package): `PSP/GAME/FasterARK`, `PSP/GAME/DC10`, `PSP/GAME/CustomIPL`, `PSP/SAVEDATA/ARK_01234`;
  - the official 6.61 update in the **root** of the memory stick, named **`661.PBP`** (PSP Go: **`661GO.PBP`**). Links to Sony's server are in the [README](../README.md#prepare-the-psp-firmware-661).
- `PSP/GAME/ARK150on660` is only for the PSP 1000 (the ARK-150 addon is 1000-only).

During installation don't press anything and never turn the PSP off until the app says it's done.

## 1. Uninstall PROMOD

1. XMB › *Game › Memory Stick* › **PROMOD-C2p** → choose **Uninstall**.
2. Restart the PSP.
3. *Settings › System Settings › System Information* must show plain **6.60**, without "PROMOD".

## 2. Start ARK-5 with FasterARK

1. *Game › Memory Stick* › **FasterARK** → run it and wait until it returns to the XMB.
2. System Information must show **6.60 ARK-5 Live**. "Live" means ARK is running only for now: **don't turn the PSP off** until step 4 is done.

## 3. Install DC10

1. Switch to the standard XMB theme: *Settings › Theme Settings › Theme* (we installed DC10 with the classic theme).
2. *Game › Memory Stick* › **DC10** (ARK DC installer) → run it and wait until it finishes.
3. A `TM` folder appears on the memory stick. **Never delete it**: it is your emergency way into DC10.

## 4. Install the cIPL

1. *Game › Memory Stick* › **CustomIPL** (ARK cIPL Flasher) → run it. Under ARK it opens normally.
2. It should say **New cIPL**. Press **X** to install the cIPL.
3. Messages you may see:
   - "After install your PSP will be 'bricked', turn on holding LT…" — expected: until step 5 is done, the PSP only boots with **L** held.
   - A warning about Infinity — also fine, DC10 is already installed.
   - "DCARK MISSING, INSTALL IT FIRST!" — step 3 didn't finish. Repeat it.
4. Wait for "Install complete". The PSP restarts.

## 5. Install 6.61 with ARK-5

1. Turn the PSP **fully off**: hold the power switch up for 3–5 seconds. Not sleep mode.
2. **Hold L** and turn the PSP on. The DC10 menu opens.
3. Choose **Install 6.61 ARK** and wait. It takes a few minutes; the PSP restarts by itself.
4. System Information must show **6.61 ARK-5 cIPL**. ARK-5 now starts by itself on every boot.

## 6. Afterwards

- Start Nemo from the XMB item **"Nemo Launcher"**. On the E1000 it is in the **Game** column.
- You can delete from the memory stick: `PSP/GAME/PROMODFW` (and other PROMOD folders), the PROMOD files in `SEPLUGINS`, `661.PBP` in the root, `PSP/GAME/FasterARK`, `PSP/GAME/CustomIPL`.
- **Keep**: the `TM` folder, `PSP/SAVEDATA/ARK_01234` and `PSP/GAME/DC10`.

## If something goes wrong

- The PSP doesn't start or hangs on a black screen: turn it fully off and on holding **L**. DC10 opens; repeat step 5.
- The DC10 menu doesn't appear: check that the memory stick is inserted and has the `TM` folder.
- CustomIPL crashes on start: ARK isn't running. Check that PROMOD is uninstalled (step 1) and repeat step 2.
