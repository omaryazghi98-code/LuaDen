#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace playden::steam {

// One version pin from LuaTools' setManifestid(depot, "manifest", size) syntax.
// The parser intentionally does not interpret addappid keys, addtoken, or any other Lua statement.
struct LuaManifestPin {
  std::uint32_t depot_id = 0;
  std::uint64_t manifest_gid = 0;
  std::uint64_t size_on_disk = 0;  // 0 means the Lua file did not include a size.
};

// A read-only projection of the version pins in a LuaTools file. This is not a Lua runtime.
struct LuaManifestProfile {
  std::vector<LuaManifestPin> pins;
};

// Extract active setManifestid calls only. Full-line Lua comments and all non-pin statements are ignored.
// A malformed pin or conflicting duplicate fails the whole import; no Lua code is executed.
bool ParseLuaManifestProfile(std::string_view source, LuaManifestProfile* out,
                             std::string* reason = nullptr);

inline constexpr std::size_t kMaxLuaManifestProfileBytes = 2u * 1024u * 1024u;
inline constexpr std::size_t kMaxLuaManifestPins = 250;

}  // namespace playden::steam
