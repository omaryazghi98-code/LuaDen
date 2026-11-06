#include "steam/backend/lua_manifest_profile.h"

#include <charconv>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>

namespace playden::steam {
namespace {

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
    text.remove_suffix(1);
  return text;
}

bool Fail(std::string* reason, const char* message) {
  if (reason) *reason = message;
  return false;
}

bool ReadUnsigned(std::string_view* input, std::uint64_t* value) {
  *input = Trim(*input);
  const std::string_view text = *input;
  std::size_t digits = 0;
  while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') ++digits;
  if (digits == 0) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + digits, *value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + digits) return false;
  input->remove_prefix(digits);
  return true;
}

bool Consume(std::string_view* input, char expected) {
  *input = Trim(*input);
  if (input->empty() || input->front() != expected) return false;
  input->remove_prefix(1);
  return true;
}

bool ParsePin(std::string_view line, LuaManifestPin* pin) {
  constexpr std::string_view name = "setManifestid";
  if (line.substr(0, name.size()) != name) return false;
  line.remove_prefix(name.size());
  if (!line.empty() && line.front() != ' ' && line.front() != '\t' && line.front() != '(') return false;
  if (!Consume(&line, '(')) return false;

  std::uint64_t depot = 0, gid = 0, size = 0;
  if (!ReadUnsigned(&line, &depot) || depot == 0 || depot > std::numeric_limits<std::uint32_t>::max() ||
      !Consume(&line, ',') || !Consume(&line, '"') ||
      !ReadUnsigned(&line, &gid) || gid == 0 || !Consume(&line, '"')) return false;

  line = Trim(line);
  if (!line.empty() && line.front() == ',') {
    line.remove_prefix(1);
    if (!ReadUnsigned(&line, &size) || size == 0) return false;
  }
  if (!Consume(&line, ')')) return false;
  line = Trim(line);
  if (!line.empty() && line.substr(0, 2) == "--") line = {};
  if (!line.empty()) return false;

  pin->depot_id = static_cast<std::uint32_t>(depot);
  pin->manifest_gid = gid;
  pin->size_on_disk = size;
  return true;
}

}  // namespace

bool ParseLuaManifestProfile(std::string_view source, LuaManifestProfile* out, std::string* reason) {
  if (!out) return Fail(reason, "No output profile was supplied.");
  *out = {};
  if (reason) reason->clear();
  if (source.empty()) return Fail(reason, "The Lua file is empty.");
  if (source.size() > kMaxLuaManifestProfileBytes)
    return Fail(reason, "The Lua file exceeds the import size limit.");

  std::map<std::uint32_t, LuaManifestPin> pins;
  std::size_t lines = 0;
  while (!source.empty()) {
    if (++lines > 200000) return Fail(reason, "The Lua file has too many lines.");
    const auto newline = source.find('\n');
    std::string_view line = Trim(source.substr(0, newline));
    source = newline == std::string_view::npos ? std::string_view{} : source.substr(newline + 1);

    if (line.empty() || line.substr(0, 2) == "--") continue;
    constexpr std::string_view name = "setManifestid";
    if (line.substr(0, name.size()) != name) continue;
    if (line.size() > name.size() && line[name.size()] != ' ' &&
        line[name.size()] != '\t' && line[name.size()] != '(') continue;

    LuaManifestPin pin;
    if (!ParsePin(line, &pin)) return Fail(reason, "A setManifestid line is malformed or contains unsupported code.");
    const auto found = pins.find(pin.depot_id);
    if (found != pins.end()) {
      if (found->second.manifest_gid != pin.manifest_gid ||
          (found->second.size_on_disk != 0 && pin.size_on_disk != 0 &&
           found->second.size_on_disk != pin.size_on_disk))
        return Fail(reason, "The Lua file contains conflicting pins for one depot.");
      if (found->second.size_on_disk == 0) found->second.size_on_disk = pin.size_on_disk;
      continue;
    }
    if (pins.size() >= kMaxLuaManifestPins)
      return Fail(reason, "The Lua file contains too many manifest pins.");
    pins.emplace(pin.depot_id, pin);
  }

  if (pins.empty()) return Fail(reason, "No active setManifestid pins were found.");
  out->pins.reserve(pins.size());
  for (const auto& [id, pin] : pins) {
    (void)id;
    out->pins.push_back(pin);
  }
  return true;
}

namespace {

bool ParseStorePin(std::string_view text, LuaManifestPin* pin) {
  const auto first = text.find(':');
  const auto second = first == std::string_view::npos ? first : text.find(':', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      text.find(':', second + 1) != std::string_view::npos) return false;
  const auto depot_text = text.substr(0, first);
  const auto gid_text = text.substr(first + 1, second - first - 1);
  const auto size_text = text.substr(second + 1);
  std::uint32_t depot = 0;
  std::uint64_t gid = 0, size = 0;
  const auto d = std::from_chars(depot_text.data(), depot_text.data() + depot_text.size(), depot);
  const auto g = std::from_chars(gid_text.data(), gid_text.data() + gid_text.size(), gid);
  const auto z = std::from_chars(size_text.data(), size_text.data() + size_text.size(), size);
  if (d.ec != std::errc{} || d.ptr != depot_text.data() + depot_text.size() || depot == 0 ||
      g.ec != std::errc{} || g.ptr != gid_text.data() + gid_text.size() || gid == 0 ||
      z.ec != std::errc{} || z.ptr != size_text.data() + size_text.size()) return false;
  *pin = LuaManifestPin{depot, gid, size};
  return true;
}

bool StoreFail(std::string* reason, const std::string& message) {
  if (reason) *reason = message;
  return false;
}

}  // namespace

bool LoadLuaManifestProfileStore(const std::filesystem::path& path, LuaManifestProfileLibrary* out,
                                 std::string* reason) {
  if (!out) return StoreFail(reason, "No profile-library output was supplied.");
  out->clear();
  if (reason) reason->clear();
  if (path.empty()) return StoreFail(reason, "The profile-library path is empty.");

  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) return StoreFail(reason, "The profile library could not be inspected: " + error.message());
    return true;
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) return StoreFail(reason, "The profile library could not be inspected: " + error.message());
  if (size > kMaxLuaManifestProfileStoreBytes)
    return StoreFail(reason, "The profile library exceeds the 8 MiB limit.");

  std::ifstream input(path, std::ios::binary);
  if (!input) return StoreFail(reason, "The profile library could not be opened.");
  std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) return StoreFail(reason, "The profile library could not be read.");
  if (contents.size() > kMaxLuaManifestProfileStoreBytes)
    return StoreFail(reason, "The profile library exceeds the 8 MiB limit.");

  LuaManifestProfileLibrary loaded;
  std::string_view remaining(contents);
  while (!remaining.empty()) {
    const auto newline = remaining.find('\n');
    std::string_view line = remaining.substr(0, newline);
    remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;

    const auto tab = line.find('\t');
    if (tab == std::string_view::npos) return StoreFail(reason, "A profile-library row is malformed.");
    const auto app_text = line.substr(0, tab);
    std::string_view pin_text = line.substr(tab + 1);
    std::uint32_t app = 0;
    const auto parsed_app = std::from_chars(app_text.data(), app_text.data() + app_text.size(), app);
    if (parsed_app.ec != std::errc{} || parsed_app.ptr != app_text.data() + app_text.size() || app == 0)
      return StoreFail(reason, "A profile-library app ID is invalid.");

    LuaManifestProfile profile;
    std::map<std::uint32_t, LuaManifestPin> pins;
    while (!pin_text.empty()) {
      const auto comma = pin_text.find(',');
      const auto item = pin_text.substr(0, comma);
      pin_text.remove_prefix(comma == std::string_view::npos ? pin_text.size() : comma + 1);
      LuaManifestPin pin;
      if (!ParseStorePin(item, &pin)) return StoreFail(reason, "A stored manifest pin is malformed.");
      if (!pins.emplace(pin.depot_id, pin).second) return StoreFail(reason, "A stored manifest pin is duplicated.");
      if (pins.size() > kMaxLuaManifestPins) return StoreFail(reason, "A stored profile contains too many pins.");
    }
    if (pins.empty()) return StoreFail(reason, "A stored profile contains no pins.");
    if (loaded.size() >= kMaxLuaManifestProfiles) return StoreFail(reason, "The profile library contains too many games.");
    if (!loaded.emplace(app, LuaManifestProfile{}).second)
      return StoreFail(reason, "The profile library contains a duplicate app ID.");
    auto& saved = loaded.at(app).pins;
    saved.reserve(pins.size());
    for (const auto& [id, pin] : pins) {
      (void)id;
      saved.push_back(pin);
    }
  }
  *out = std::move(loaded);
  return true;
}

bool SaveLuaManifestProfileStore(const std::filesystem::path& path, const LuaManifestProfileLibrary& profiles,
                                 std::string* reason) {
  if (reason) reason->clear();
  if (path.empty()) return StoreFail(reason, "The profile-library path is empty.");
  if (profiles.size() > kMaxLuaManifestProfiles)
    return StoreFail(reason, "The profile library contains too many games.");

  std::string contents;
  for (const auto& [app, profile] : profiles) {
    if (app == 0 || profile.pins.empty() || profile.pins.size() > kMaxLuaManifestPins)
      return StoreFail(reason, "A profile-library entry is invalid.");
    contents += std::to_string(app);
    contents.push_back('\t');
    std::map<std::uint32_t, LuaManifestPin> pins;
    for (const auto& pin : profile.pins) {
      if (pin.depot_id == 0 || pin.manifest_gid == 0 ||
          !pins.emplace(pin.depot_id, pin).second)
        return StoreFail(reason, "A profile contains an invalid or duplicate manifest pin.");
    }
    bool first = true;
    for (const auto& [id, pin] : pins) {
      if (!first) contents.push_back(',');
      contents += std::to_string(id) + ":" + std::to_string(pin.manifest_gid) + ":" + std::to_string(pin.size_on_disk);
      first = false;
    }
    contents.push_back('\n');
    if (contents.size() > kMaxLuaManifestProfileStoreBytes)
      return StoreFail(reason, "The profile library exceeds the 8 MiB limit.");
  }

  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  if (error) return StoreFail(reason, "The profile-library directory could not be created: " + error.message());

  if (contents.empty()) {
    std::filesystem::remove(path, error);
    if (error) return StoreFail(reason, "The empty profile library could not be removed: " + error.message());
    return true;
  }

  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return StoreFail(reason, "The profile-library temporary file could not be opened.");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
      output.close();
      std::filesystem::remove(temporary, error);
      return StoreFail(reason, "The profile library could not be written.");
    }
  }
  if (std::rename(temporary.string().c_str(), path.string().c_str()) != 0) {
    const int saved_errno = errno;
    std::filesystem::remove(temporary, error);
    return StoreFail(reason, "The profile library could not be committed (errno " + std::to_string(saved_errno) + ").");
  }
  return true;
}

}  // namespace playden::steam
