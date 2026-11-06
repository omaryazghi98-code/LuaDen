#include "steam/backend/lua_manifest_profile.h"

#include <charconv>
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

}  // namespace playden::steam
