# SwitchDiscord 1.0.0

Unofficial Discord **text** client for Nintendo Switch (homebrew `.nro`). Sign in with **your Discord account** (OAuth2). Not affiliated with Discord — use at your own risk.

## Features

- Direct messages, friends (open DM), servers & text channels
- Message history + polling (~3s) for new messages
- Token refresh via `auth.json` on SD card
- Discord-style dark UI, custom homebrew icon

## Requirements

- Modded Switch (Atmosphere or compatible CFW), Wi‑Fi
- `sdmc:/switch/switchdiscord/`:
  - `switchdiscord.nro`
  - `config.ini` (`client_id`, `redirect_uri`)
  - `DejaVuSans.ttf` (or `font.ttf`)

## OAuth setup (one time)

1. [Discord Developer Application](https://discord.com/developers/applications) → OAuth2
2. Enable **GitHub Pages** on this repo: Settings → Pages → branch **main**, folder **`/docs`**. OAuth redirect URL:
   `https://stimpydev.github.io/switch-discord/oauth-relay.html` (Discord app + `config.ini`; Pages folder **`/docs`**, no extra `/docs/` in the URL)
3. First launch: scan the **QR code** on Switch with your phone, approve login
4. Phone + Switch on the **same Wi‑Fi**

See `config.example.ini` for all options.

## Controls

| Button | Action |
|--------|--------|
| **-** | Cycle DM / Friends / Guild |
| **A** | Open friend DM, or send message |
| **L / R** | Previous / next in list |
| **X / Y** | Previous / next server (guild tab) |
| **D-pad** | Scroll chat |
| **+** | Quit |

## Build (devkitPro)

```bash
source /etc/profile.d/devkit-env.sh
cd switch-discord/build && cmake .. -G "Unix Makefiles" && make -j8
```

Output: `build/switchdiscord.nro`

## Known limits

- No voice; no live Gateway yet (polling only)
- Default OAuth scopes: `identify guilds` (`messages.read` / `relationships.read` are restricted and cause `invalid_scope` for most apps)
- Unofficial client — account risk

## License

MIT — Discord is a trademark of Discord Inc.
