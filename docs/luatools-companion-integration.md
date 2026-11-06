# LuaTools Companion Integration

Status: design baseline for `feature/luatools-bridge`.

## Goal

Use a Windows PC running LuaTools as the main management workstation and a jailbroken PS5 running LuaDen/Playden as a companion target.

The first implementation should reuse Playden's existing LAN web service. Do not add a second PS5 HTTP server or duplicate pairing and file-transfer code unless a concrete missing capability requires it.

LuaTools remains responsible for its own Windows-side library and workflows. The companion connection is a separate adapter initially; a later LuaTools fork/plugin can surface it inside the main app.

## Current LuaDen capabilities

The PS5 app already has a web service under `src/web/`:

- `server.cpp` / `server.h`: small HTTP/1.1 server. By default it tries ports 7031–7039 and listens on the console's network interfaces.
- `service.cpp` / `service.h`: owns the server, status snapshot, browser page and API router.
- `pairing.cpp` / `pairing.h`: six-digit, time-limited pairing code; lockout after repeated wrong attempts; random session token after successful pairing.
- `files_api.cpp` / `files_api.h`: paired access to a catalog of game folders, file listing, download/ZIP, upload, folder creation, deletion, restore and size summaries.
- `src/web/page/index.html`: existing browser client.

The file API currently exposes:

| Method | Route | Purpose |
| --- | --- | --- |
| POST | `/api/pair/start` | Ask Playden to show a pairing code |
| POST | `/api/pair/confirm?code=<six digits>` | Exchange the displayed code for a session cookie |
| GET | `/api/pair/status` | Check whether the current client is paired |
| GET | `/api/files/games` | Read the game catalog |
| GET | `/api/files/game?game=<id>` | Read one game's metadata and available roots |
| GET | `/api/files/list?game=<id>&root=<root>&path=<path>` | List a folder |
| GET | `/api/files/download?game=<id>&root=<root>&path=<path>` | Download one file |
| GET | `/api/files/zip?game=<id>&root=<root>&path=<path>` | Download a folder as ZIP |
| GET | `/api/files/tree?game=<id>&root=<root>&path=<path>` | Summarize a folder |
| POST | `/api/files/upload?game=<id>&root=<root>&path=<path>` | Upload/replace one file |
| POST | `/api/files/mkdir?game=<id>&root=<root>&path=<path>` | Create a folder |
| POST | `/api/files/delete?game=<id>&root=<root>&path=<path>` | Delete a file or folder |
| POST | `/api/files/restore?game=<id>&root=<root>&path=<path>` | Restore a previous file version |

State-changing pairing and file actions require the `X-Playden-Action: files` header. The pairing confirmation response sets the `playden_files` HttpOnly cookie; subsequent paired requests send that cookie.

## Recommended connection flow

1. The Windows companion asks for the PS5's IPv4 address and web-service port (7031–7039; use the port shown in Playden's Web access settings).
2. It calls `POST /api/pair/start` with `X-Playden-Action: files`.
3. The user reads the six-digit code shown by Playden on the TV.
4. The Windows companion calls `POST /api/pair/confirm?code=...` with the same action header and retains the returned cookie in a `.NET HttpClientHandler.CookieContainer`.
5. It calls `GET /api/pair/status` and `GET /api/files/games`, then exposes the returned game catalog and allowed file roots to the desktop UI.
6. Transfers use the existing file endpoints; progress is measured by the Windows client while streaming bytes.

A native .NET client should not send an `Origin` header. If it does send one, Playden requires it to match the request's `Host` authority exactly.

## Important current limitations

- The service is available only while Playden is running. The README says starting a game closes Playden, so the Windows companion must show an offline/disconnected state during gameplay and reconnect when Playden is opened again.
- Pairing tokens are held in the running Playden process. A Playden restart requires pairing again.
- The current API is primarily a file-management API. It does not expose a remote import command, remote install queue, game launch command or general LuaTools control API.
- The current `FileCatalog` contains game identity/status and file roots, but it is not a complete mirror of every internal Playden operation.
- The web service uses plain HTTP. Treat it as trusted-LAN-only; do not expose it to the public internet.

## Proposed implementation phases

### Phase 1 — Windows companion transport

Create a small .NET 8 client that can:

- configure and remember the PS5 IPv4 address;
- request pairing and submit the on-screen code;
- retain the session cookie for the current Playden session;
- display connection state and the game catalog;
- list and transfer files through the existing paired API;
- handle disconnects, timeouts, 401 (re-pair), 503 (Playden busy) and interrupted transfers.

This phase should not require changes to the Playden server.

### Phase 2 — Safe import workflow

Add a narrow, versioned API for a user-confirmed import workflow only if the existing file endpoints are insufficient. It should stage files under Playden's designated import area and hand the request to the existing Local library/import controller. It must not accept arbitrary filesystem paths or execute uploaded content.

### Phase 3 — LuaTools-facing adapter

Keep LuaTools installed and running on Windows. Add an optional UI entry point or plugin/adapter that opens the companion panel and passes app/game identifiers and user-selected local files. Do not assume LuaTools exposes a public extension API; verify the specific extension point before binding to it.

### Phase 4 — Native PS5 companion UX

Add a LuaTools/PC connection state and transfer visibility to LuaDen only if the Windows client needs PS5-side UI beyond the existing Web access and Queue screens.

## Security and scope

- Keep the existing six-digit pairing flow and random session token; never hard-code a token.
- Store any Windows-side session material using Windows-protected storage, and discard it when the Playden session ends or pairing is revoked.
- Keep the current Host/Origin checks and custom action-header requirement. Add tests before exposing new state-changing routes.
- Use explicit game IDs and allow-listed file roots; never accept arbitrary absolute paths from the PC.
- Transfers must be user-initiated, bounded and resumable where practical.
- This integration is for managing and transferring game files the user is licensed to use, plus saves and ordinary configuration. It must not add features that bypass Steam ownership/entitlement checks or defeat DRM/anti-cheat protections.
