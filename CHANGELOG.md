# Changelog

## 1.0.0 — Switchcord (stable)

- Renamed from SwitchDiscord to **Switchcord** (`switchcord.nro`, install folder `switch/switchcord/`)
- Fixed API auth header bug (empty servers/DMs after login)
- OAuth via QR → `login.html`, scopes `identify guilds`
- Discord-style UI, token refresh, rate-limit retry
- Cleaner login copy on Switch and OAuth relay pages

## 1.0.2

- OAuth scope `dm_channels.read` for DMs (re-login required)
- Auto refresh token on HTTP 401; fix exit crash (curl cleanup order)
- **X / Y** selects servers from any tab

## 1.0.1

- Removed legacy `switch/switchdiscord/` install path (use `switch/switchcord/` only)

## Pre-release history

Earlier builds were published as SwitchDiscord 0.2.x–1.0.3 on the same repo.
