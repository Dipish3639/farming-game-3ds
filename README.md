# Pixel Farm 🌱

A cozy pixel-art farming game for the **Nintendo 3DS**, inspired by Stardew Valley — but just the farming.

Clear your overgrown land, till the soil, plant seeds, water them every day, and ship your harvest for gold. Crops change with the seasons, rain waters your fields for you, and sprinklers take care of watering once you can afford them.

## Features

- **Farm view on the top screen**, HUD and backpack on the touch screen
- **12 crops across 4 seasons** — Parsnip, Potato, Cauliflower, Strawberry, Tomato, Melon, Blueberry, Corn, Pumpkin, Eggplant, Cranberry and Winter Kale. Some regrow after harvest.
- **Tools**: Hoe, Watering Can, Scythe, Pickaxe
- **Day/night cycle** (6 AM – 2 AM, about 12 real minutes per day) with an energy bar. Stay up past 2 AM and you'll pass out!
- **Seasons** — 28 days each; the farm changes colour and out-of-season crops wither
- **Weather** — rain waters every tilled tile, winter brings snow
- **Seed Stall** shop with seasonal seeds, sprinklers and tool upgrades (Big Watering Can, Steel Hoe)
- **Shipping bin** — sell crops overnight and get a shipping report each morning
- **Eat your crops** to restore energy
- Autosaves every night to `sdmc:/3ds/pixelfarm/save.dat`

## Controls

| Button | Action |
| --- | --- |
| Circle Pad / D-Pad | Move |
| Hold B | Run |
| A | Use tool / plant / harvest / ship / open shop / go to bed |
| L / R or tap the backpack | Choose item |
| Y | Eat the selected crop |
| START | Menu (save, quit) |

**Tips:** Face the house door and press A to sleep. Select a crop and press A at the shipping bin to sell the whole stack. Refill the watering can at the pond.

## Installing on a 3DS

You need a 3DS with custom firmware and the Homebrew Launcher.

1. Download `pixelfarm.3dsx` from the latest **Build** run on GitHub Actions (the `pixelfarm-3dsx` artifact), or build it yourself (below).
2. Copy it to `sdmc:/3ds/` on your SD card.
3. Launch it from the Homebrew Launcher.

It also runs in 3DS emulators that load `.3dsx` files (e.g. Citra / Lime3DS / Azahar).

## Building

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `3ds-dev` package group, then:

```sh
make
```

Or with Docker:

```sh
docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm make
```

## How it's made

Everything is drawn with a small software renderer (`source/gfx.c`) into plain pixel buffers which are copied to the 3DS framebuffers each frame — no image assets needed. All pixel art lives as text grids in `source/sprites.c`, recoloured per crop and per season.

Because the game code doesn't depend on the 3DS, it can also run on a PC. `tools/pc_sim.c` plays a scripted session (till, plant, water, sleep, harvest, ship, shop, seasons, save/load) and writes screenshots:

```sh
cc -O2 -Isource source/game.c source/gfx.c source/sprites.c source/data.c tools/pc_sim.c -lm -o pc_sim
mkdir -p shots && ./pc_sim shots
```

| File | What's in it |
| --- | --- |
| `source/main_3ds.c` | 3DS entry point: input, framebuffer output |
| `source/game.c` | Game state, farming rules, day cycle, shop, save/load, rendering |
| `source/sprites.c` | Pixel art and procedurally generated ground tiles |
| `source/data.c` | Crop table and item names |
| `source/gfx.c` | Software renderer and 5×7 pixel font |
