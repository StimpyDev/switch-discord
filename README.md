# Switchcord 1.0.1

Unofficial Discord text client for Nintendo Switch (homebrew). Not made or endorsed by Discord.

## SD card setup

Folder: `switch/switchcord/`

| File | Notes |
|------|--------|
| `switchcord.nro` | From `build/switchcord.nro` |
| `config.ini` | Copy from `config.example.ini` |
| `DejaVuSans.ttf` | Required font |

## Discord app (one-time)

1. [Developer Portal](https://discord.com/developers/applications) → OAuth2 → copy **Client ID**
2. Redirect URL (must match `config.ini` exactly):

   `https://stimpydev.github.io/switch-discord/oauth-relay.html`

3. GitHub Pages on this repo: branch **main**, folder **`/docs`**

```ini
client_id=YOUR_ID
redirect_uri=https://stimpydev.github.io/switch-discord/oauth-relay.html
oauth_scopes=identify guilds
```

Sign in: scan QR on Switch, approve on phone (same Wi‑Fi).

## Controls

| Button | |
|--------|---|
| **−** | DMs / Friends / Servers |
| **L / R** | List |
| **A** | Open DM or send message |
| **X / Y** | Previous / next server |
| **+** | Quit |

## Build

MSYS2 + devkitPro:

```bash
source /etc/profile.d/devkit-env.sh
cd switch-discord/build
cmake .. -G "Unix Makefiles" && make -j8
```

Output: `build/switchcord.nro`

## Limits

- Text only, message polling (~3s)
- Friends list needs scopes Discord does not give most apps
- Unofficial client — use at your own risk

MIT license. Discord is a trademark of Discord Inc.
