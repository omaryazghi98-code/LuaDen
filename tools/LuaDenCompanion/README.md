# LuaDen Companion for Windows

A standalone Windows desktop companion for Playden's paired LAN API. It lives under the tools directory in the LuaDen repository and builds independently; it is not part of LuaTools and does not require LuaTools source changes, references, packages, or build steps.

## Features

- Pair a Windows PC with Playden using the six-digit code shown on the PS5.
- Load the Playden game catalog and select a Steam game.
- Browse the file roots exposed by Playden.
- Download a selected file from the PS5 to the PC.
- Import the manifest-pin portion of a LuaTools .lua profile for a selected Steam game.

The pairing cookie is kept in memory only. The app targets the PS5's IPv4 address and port directly and should be used only on a trusted local network.

## Build

Requirements: Windows 10/11 and the .NET 8 SDK.

From the repository root:

```powershell
dotnet build .\tools\LuaDenCompanion\LuaDenCompanion.csproj -c Release
dotnet run --project .\tools\LuaDenCompanion\LuaDenCompanion.csproj
```

## Profile import scope

The current Playden endpoint imports only active setManifestid(depot, "manifest", size) declarations. It does not execute uploaded Lua. Other LuaTools statements are left in the source file on the PC and are not applied by Playden. Steam's own account, entitlement, and depot-access checks remain authoritative.

This companion does not implement DRM circumvention, Steam ownership bypasses, unlocker modes, or executable Lua injection. It does not modify LuaTools or its existing features.
