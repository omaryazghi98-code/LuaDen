#include "steam/backend/lua_manifest_profile.h"

#include <cassert>
#include <cstdint>
#include <string>

using namespace playden::steam;

int main() {
  LuaManifestProfile profile;
  std::string reason;

  const std::string valid =
      "-- comment\r\n"
      "addappid(123456, 1, \"not-imported\")\r\n"
      "setManifestid(456001, \"987654321012345678\", 123456789) -- current build\r\n"
      "setManifestid ( 456002 , \"987654321012345679\" )\r\n"
      "-- setManifestid(456003, \"987654321012345680\", 10)\r\n"
      "addtoken(999999)\r\n";
  assert(ParseLuaManifestProfile(valid, &profile, &reason));
  assert(reason.empty());
  assert(profile.pins.size() == 2);
  assert(profile.pins[0].depot_id == 456001);
  assert(profile.pins[0].manifest_gid == 987654321012345678ULL);
  assert(profile.pins[0].size_on_disk == 123456789ULL);
  assert(profile.pins[1].depot_id == 456002);
  assert(profile.pins[1].manifest_gid == 987654321012345679ULL);
  assert(profile.pins[1].size_on_disk == 0);

  assert(!ParseLuaManifestProfile("addappid(123, 1, \"key\")\n", &profile, &reason));
  assert(!reason.empty());
  assert(!ParseLuaManifestProfile("setManifestid(1, \"2\") os.execute('x')\n", &profile, &reason));
  assert(!ParseLuaManifestProfile("setManifestid(1, \"2\", 0)\n", &profile, &reason));
  assert(!ParseLuaManifestProfile("setManifestid(0, \"2\")\n", &profile, &reason));
  assert(!ParseLuaManifestProfile("setManifestid(4294967296, \"2\")\n", &profile, &reason));
  assert(!ParseLuaManifestProfile("setManifestid(1, \"0\")\n", &profile, &reason));
  assert(!ParseLuaManifestProfile("setManifestid(1, \"2\")\nsetManifestid(1, \"3\")\n", &profile, &reason));

  const std::string duplicate =
      "setManifestid(1, \"2\")\n"
      "setManifestid(1, \"2\", 42)\n";
  assert(ParseLuaManifestProfile(duplicate, &profile, &reason));
  assert(profile.pins.size() == 1 && profile.pins[0].size_on_disk == 42);

  assert(!ParseLuaManifestProfile("", &profile, &reason));
  std::string oversized(kMaxLuaManifestProfileBytes + 1, ' ');
  assert(!ParseLuaManifestProfile(oversized, &profile, &reason));
  assert(!ParseLuaManifestProfile(valid, nullptr, &reason));
  return 0;
}
