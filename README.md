# Switchcord 1.0.3

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

**Scopes:** Only use public scopes. `dm_channels.read`, `messages.read`, and `relationships.read` are partner-only — Discord returns `invalid_scope`.

Sign in: scan QR on Switch, approve on phone (same Wi‑Fi). Delete `auth.json` after changing scopes.

### Channel list blocked?

Discord often blocks `/guilds/.../channels` for OAuth apps. Set IDs from desktop Discord (Settings → Advanced → Developer Mode, then right‑click server/channel → Copy ID):

```ini
guild_id=123456789012345678
channel_id=123456789012345678
```

## Controls

| Button | |
|--------|---|
| **−** | Servers / info |
| **L / R** | Channel list |
| **A** | Open channel or send message |
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

- OAuth: **identify** + **guilds** (profile + server list)
- No DMs or friends without Discord partner approval
- Message history may be blocked (403) on some channels
- Avatars are initials only (no CDN images yet)
- Live updates need Gateway (planned: `switch-wslay`)
- Unofficial client — use at your own risk

MIT license. Discord is a trademark of Discord Inc.
