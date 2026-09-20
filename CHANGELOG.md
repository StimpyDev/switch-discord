# Changelog

## 1.0.1

- OAuth login screen shows a scannable QR code instead of a long URL

## 1.0.0 — Stable

- Personal Discord login via OAuth2 (PKCE) and `auth.json` token storage
- DM list, friends list, server channels, text chat with polling (~3s)
- Automatic access-token refresh (session + periodic check)
- Rate-limit retry (HTTP 429), curl timeouts, shorter API error text
- UI fixes: tab switching, friends → DM → compose flow, guild tab reload bug
- Discord homebrew icon and NACP title

## 0.2.x — Beta

- Initial Switch homebrew client, bot mode (removed in 1.0)
