#include "steam/backend/lua_manifest_profile.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace playden::steam;
namespace fs = std::filesystem;

int main() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path base = fs::temp_directory_path() / ("playden-lua-manifest-store-" + std::to_string(stamp));
  const fs::path path = base / "profiles" / "lua-manifest-profiles.txt";
  std::string reason;

  LuaManifestProfileLibrary profiles;
  profiles[220].pins = {
      LuaManifestPin{221, 18446744073709551615ull, 123456789ull},
      LuaManifestPin{222, 987654321012345678ull, 0}
  };
  profiles[440].pins = {LuaManifestPin{441, 123456789ull, 42ull}};
  assert(SaveLuaManifestProfileStore(path, profiles, &reason));
  assert(reason.empty());
  assert(fs::exists(path));

  LuaManifestProfileLibrary loaded;
  assert(LoadLuaManifestProfileStore(path, &loaded, &reason));
  assert(reason.empty());
  assert(loaded.size() == 2);
  assert(loaded.at(220).pins.size() == 2);
  assert(loaded.at(220).pins[0].depot_id == 221);
  assert(loaded.at(220).pins[0].manifest_gid == 18446744073709551615ull);
  assert(loaded.at(220).pins[0].size_on_disk == 123456789ull);
  assert(loaded.at(220).pins[1].manifest_gid == 987654321012345678ull);
  assert(loaded.at(220).pins[1].size_on_disk == 0);
  assert(loaded.at(440).pins[0].depot_id == 441);

  // A missing file is an empty library, not a fatal first-run error.
  LuaManifestProfileLibrary missing;
  assert(LoadLuaManifestProfileStore(base / "missing.txt", &missing, &reason));
  assert(missing.empty());

  // Corrupt and duplicate rows fail without returning a partial library.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "220\t221:123:42,221:456:42\n";
  }
  loaded.clear();
  assert(!LoadLuaManifestProfileStore(path, &loaded, &reason));
  assert(loaded.empty() && !reason.empty());

  // Invalid profiles are refused before the durable file is replaced.
  LuaManifestProfileLibrary invalid;
  invalid[220].pins = {LuaManifestPin{0, 12, 0}};
  assert(!SaveLuaManifestProfileStore(path, invalid, &reason));
  assert(!reason.empty());

  // Empty means remove the profile store (all games cleared).
  assert(SaveLuaManifestProfileStore(path, {}, &reason));
  assert(!fs::exists(path));

  fs::remove_all(base);
  return 0;
}
