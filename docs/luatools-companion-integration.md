# LuaTools ↔ Playden integration

Status: first native manifest-profile path implemented on `feature/luatools-bridge`.

## Current architecture

Playden remains the native PS5 application and the source of truth for Steam account, library, depot selection and installs. LuaTools runs on the Windows PC and can send a selected LuaTools profile over Playden's existing paired LAN service.

The integration deliberately does not execute uploaded Lua. It extracts only active `setManifestid(depot, "manifest", size)` declarations. Other Lua statements, `addappid` keys and `addtoken` directives are ignored.

## Implemented import route

`POST /api/files/lua-profile?game=<steam-app-id>`

Requirements:

- The PC must be paired using the existing six-digit pairing flow.
- The request must include `X-Playden-Action: files`.
- The body is the raw UTF-8 `.lua` file.
- The file is limited to 2 MiB and the parser accepts at most 250 distinct manifest pins.

Successful response:

```json
{"ok":true,"app":12210,"pins":2}
```

Only a Steam game present in Playden's current library catalog can be targeted. The route does not accept arbitrary paths.

## What happens after import

1. The route validates the pairing and action header, reads the bounded request body and parses the profile.
2. The Web worker queues the parsed profile; the Playden controller applies it to the model on the main thread.
3. A new install or a game update carries the profile's manifest pins through the install queue.
4. Depot selection applies a pin only to a depot already listed by the game's current app metadata.
5. Playden's existing account-ownership, DLC, OS, branch and depot-access checks remain in place. A pin cannot create a depot or grant an entitlement. Steam's depot-access response remains authoritative.
6. The manifest pins are written to the install record after a successful install/update, so later updates can reuse them.

Imported profiles are persisted beside Playden's per-user game settings in `lua-manifest-profiles.txt`, and are reused by later installs and updates after a restart. This first version keeps one active profile per Steam app; LuaTools' multi-build vault and a native PS5 profile selector are not implemented yet.

## LuaTools desktop action

The `xLuaTools` branch `feature/ps5-luaden-companion` now has an **Import Lua profile to Playden** action in the PS5 Companion page. Select a Steam game, choose a local `.lua` file, and the companion posts it to the endpoint above using the existing in-memory paired session cookie.

## Existing Playden APIs

The LAN service still exposes the existing pairing and file-management APIs:

| Method | Route | Purpose |
| --- | --- | --- |
| POST | `/api/pair/start` | Ask Playden to show a pairing code |
| POST | `/api/pair/confirm?code=<six digits>` | Exchange the displayed code for a session cookie |
| GET | `/api/pair/status` | Check whether the current client is paired |
| GET | `/api/files/games` | Read the game catalog |
| GET | `/api/files/game?game=<id>` | Read one game's metadata and available roots |
| GET | `/api/files/list?game=<id>&root=<root>&path=<path>` | List a folder |
| POST | `/api/files/lua-profile?game=<steam-app-id>` | Import LuaTools manifest pins |
| GET | `/api/files/download?game=<id>&root=<root>&path=<path>` | Download one file |
| GET | `/api/files/zip?game=<id>&root=<root>&path=<path>` | Download a folder as ZIP |
| POST | `/api/files/upload?game=<id>&root=<root>&path=<path>` | Upload/replace one file |

State-changing pairing and file actions require `X-Playden-Action: files`. Pairing confirmation sets the `playden_files` HttpOnly cookie. A native .NET client should not send an `Origin` header.

## Build and validation

The Playden bridge branch now has a macOS host build/test workflow at `.github/workflows/luatools-bridge-ci.yml`. It uses the project's documented Apple Silicon build path:

```sh
scripts/build-homebrew.sh
cmake --build --preset homebrew-host --parallel 2
ctest --preset homebrew-host --output-on-failure
```

The PS5 package still needs a device build/deploy and a live install/update test after CI succeeds. The web service uses plain HTTP; keep it on a trusted LAN and do not expose it to the public internet.

## Next native milestones

- A durable per-game profile store, independent of whether an install/update has already run.
- A Playden-native profile browser/editor and per-depot enable/disable controls.
- Import and profile status visible on the PS5 game details page.
- Port additional LuaTools workflows that map cleanly to Playden's native runtime and settings model.
