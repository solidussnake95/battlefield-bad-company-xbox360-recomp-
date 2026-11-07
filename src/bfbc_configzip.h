// bfbc - read and rewrite the FieldOfView engine setting in config.zz.
//
// config.zz is a plain ZIP holding Game.cfg and the Frost engine scripts.
// The engine applies Game.cfg's `applySettings` list at boot, so the F5 menu
// cannot change the field of view in a running session; the slider writes the
// value here instead and the menu reports that a restart is required.
//
// Only Game.cfg is touched. Every other entry is copied byte for byte (each
// entry's local region, which also keeps any data descriptor), and Game.cfg is
// stored uncompressed so no compressor is needed. Inflation uses the raw
// DEFLATE helper rexruntime already exports (stb_image's), writing into a
// caller buffer, so nothing is allocated across the DLL boundary.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace bfbc::configzip {

inline constexpr const char* kBegin = "-- bfbc port overrides begin";
inline constexpr const char* kEnd = "-- bfbc port overrides end";
inline constexpr const char* kFovKey = "Render.ForceFov";
inline constexpr int kFovMin = 45;
inline constexpr int kFovMax = 120;
// Film grain is a plain Lua assignment in Game.cfg, not an applySettings entry.
inline constexpr const char* kGrainKey = "PostProcess.FilmGrainEnable";

// ---------------------------------------------------------------------------
// Byte helpers
// ---------------------------------------------------------------------------

inline uint16_t Rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void Wr16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

inline void Wr32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
  }
}

inline void WrBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

inline void Patch32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
  }
}

inline uint32_t Crc32(const uint8_t* data, size_t size) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      table[i] = c;
    }
    ready = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Inflate
// ---------------------------------------------------------------------------

// int stbi_zlib_decode_noheader_buffer(char* out, int out_len, const char* in, int in_len)
using RawInflateFn = int (*)(char*, int, const char*, int);

inline RawInflateFn ResolveInflate() {
#if defined(_WIN32)
  // Only a successful lookup is cached: the dialog can be opened before the
  // runtime module is loaded in some launch paths, and the import is stable.
  static RawInflateFn cached = nullptr;
  if (cached) {
    return cached;
  }
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  if (!runtime) {
    return nullptr;
  }
  cached = reinterpret_cast<RawInflateFn>(
      GetProcAddress(runtime, "stbi_zlib_decode_noheader_buffer"));
  return cached;
#else
  return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// ZIP directory
// ---------------------------------------------------------------------------

struct Entry {
  size_t cd_pos = 0;  // offset of the central directory record
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint32_t csize = 0;
  uint32_t usize = 0;
  uint32_t local_offset = 0;
  uint16_t name_len = 0;
  uint16_t extra_len = 0;
  uint16_t comment_len = 0;
  size_t cd_len = 0;
};

struct Archive {
  std::vector<uint8_t> bytes;
  std::vector<Entry> entries;
  size_t cd_offset = 0;
};

// Parses out->bytes in place; the central directory must already be readable.
inline bool ParseArchive(Archive* out, std::string* error) {
  if (out->bytes.size() < 22) {
    *error = "archive is too small";
    return false;
  }

  // End of central directory: scan backwards over a possible comment.
  size_t eocd = std::string::npos;
  const size_t lowest = out->bytes.size() > (65535 + 22) ? out->bytes.size() - (65535 + 22) : 0;
  for (size_t pos = out->bytes.size() - 22;; --pos) {
    if (Rd32(&out->bytes[pos]) == 0x06054b50u) {
      eocd = pos;
      break;
    }
    if (pos == lowest) {
      break;
    }
  }
  if (eocd == std::string::npos) {
    *error = "no end-of-central-directory record";
    return false;
  }

  const uint16_t count = Rd16(&out->bytes[eocd + 10]);
  const uint32_t cd_size = Rd32(&out->bytes[eocd + 12]);
  const uint32_t cd_off = Rd32(&out->bytes[eocd + 16]);
  if (static_cast<size_t>(cd_off) + cd_size > out->bytes.size()) {
    *error = "central directory is out of range";
    return false;
  }
  out->cd_offset = cd_off;
  out->entries.clear();
  size_t pos = cd_off;
  for (uint16_t i = 0; i < count; ++i) {
    if (pos + 46 > out->bytes.size() || Rd32(&out->bytes[pos]) != 0x02014b50u) {
      *error = "central directory record " + std::to_string(i) + " is invalid";
      return false;
    }
    Entry entry;
    entry.cd_pos = pos;
    entry.flags = Rd16(&out->bytes[pos + 8]);
    entry.method = Rd16(&out->bytes[pos + 10]);
    entry.crc = Rd32(&out->bytes[pos + 16]);
    entry.csize = Rd32(&out->bytes[pos + 20]);
    entry.usize = Rd32(&out->bytes[pos + 24]);
    entry.name_len = Rd16(&out->bytes[pos + 28]);
    entry.extra_len = Rd16(&out->bytes[pos + 30]);
    entry.comment_len = Rd16(&out->bytes[pos + 32]);
    entry.local_offset = Rd32(&out->bytes[pos + 42]);
    entry.name.assign(reinterpret_cast<const char*>(&out->bytes[pos + 46]), entry.name_len);
    entry.cd_len = 46u + entry.name_len + entry.extra_len + entry.comment_len;
    if (pos + entry.cd_len > out->bytes.size()) {
      *error = "central directory record " + std::to_string(i) + " overruns the file";
      return false;
    }
    out->entries.push_back(std::move(entry));
    pos += out->entries.back().cd_len;
  }
  return true;
}

inline bool LoadArchive(const std::filesystem::path& path, Archive* out, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *error = "cannot open " + path.string();
    return false;
  }
  out->bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return ParseArchive(out, error);
}

inline bool ReadEntry(const Archive& archive, const Entry& entry, std::vector<uint8_t>* out,
                      std::string* error) {
  const std::vector<uint8_t>& bytes = archive.bytes;
  const size_t local = entry.local_offset;
  if (local + 30 > bytes.size() || Rd32(&bytes[local]) != 0x04034b50u) {
    *error = "local header for " + entry.name + " is invalid";
    return false;
  }
  const uint16_t name_len = Rd16(&bytes[local + 26]);
  const uint16_t extra_len = Rd16(&bytes[local + 28]);
  const size_t data = local + 30 + name_len + extra_len;
  if (data + entry.csize > bytes.size()) {
    *error = "data for " + entry.name + " overruns the file";
    return false;
  }
  if (entry.method == 0) {
    out->assign(bytes.begin() + data, bytes.begin() + data + entry.csize);
    return true;
  }
  if (entry.method != 8) {
    *error = entry.name + " uses unsupported compression method " + std::to_string(entry.method);
    return false;
  }
  RawInflateFn inflate = ResolveInflate();
  if (!inflate) {
    *error = "rexruntime does not export a DEFLATE decoder";
    return false;
  }
  out->assign(entry.usize, 0);
  const int produced = inflate(reinterpret_cast<char*>(out->data()), static_cast<int>(entry.usize),
                               reinterpret_cast<const char*>(&bytes[data]),
                               static_cast<int>(entry.csize));
  if (produced < 0) {
    *error = "could not inflate " + entry.name;
    return false;
  }
  out->resize(static_cast<size_t>(produced));
  return true;
}

// ---------------------------------------------------------------------------
// Game.cfg override block
// ---------------------------------------------------------------------------

inline bool BlockBounds(const std::string& text, size_t* begin, size_t* end) {
  const size_t start = text.find(kBegin);
  if (start == std::string::npos) {
    return false;
  }
  const size_t stop = text.find(kEnd, start);
  if (stop == std::string::npos) {
    return false;
  }
  *begin = start;
  *end = stop;
  return true;
}

inline std::vector<std::string> BlockLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t begin = 0;
  size_t end = 0;
  if (!BlockBounds(text, &begin, &end)) {
    return lines;
  }
  std::string block = text.substr(begin, end - begin);
  size_t pos = 0;
  while (pos <= block.size()) {
    const size_t nl = block.find('\n', pos);
    std::string line = block.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? block.size() + 1 : nl + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    const std::string trimmed = line;
    const size_t first = trimmed.find_first_not_of(" \t");
    const std::string key = (first == std::string::npos) ? std::string() : trimmed.substr(first);
    if (key.empty() || key == kBegin || key == kEnd || key.rfind("applySettings", 0) == 0 ||
        key.rfind("]=]", 0) == 0) {
      continue;
    }
    lines.push_back(key);
  }
  return lines;
}

inline std::string ReplaceBlock(const std::string& text, const std::vector<std::string>& lines) {
  std::string block = kBegin;
  if (!lines.empty()) {
    block += "\napplySettings [=[\n";
    for (const std::string& line : lines) {
      block += line + "\n";
    }
    block += "]=]\n";
  }
  block += kEnd;

  size_t begin = 0;
  size_t end = 0;
  if (!BlockBounds(text, &begin, &end)) {
    if (lines.empty()) {
      return text;
    }
    std::string out = text;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
      out.pop_back();
    }
    return out + "\n\n" + block + "\n";
  }
  return text.substr(0, begin) + block + text.substr(end + std::strlen(kEnd));
}

inline std::vector<std::string> WithFov(const std::vector<std::string>& lines, bool present,
                                        int degrees) {
  std::vector<std::string> kept;
  for (const std::string& line : lines) {
    if (line.rfind(kFovKey, 0) == 0) {
      continue;
    }
    kept.push_back(line);
  }
  if (present) {
    kept.push_back(std::string(kFovKey) + " " + std::to_string(degrees));
  }
  return kept;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

struct FovState {
  bool present = false;
  int degrees = 0;
};

inline FovState ReadFov(const std::filesystem::path& config_zz) {
  FovState state;
  Archive archive;
  std::string error;
  if (config_zz.empty() || !LoadArchive(config_zz, &archive, &error)) {
    return state;
  }
  for (const Entry& entry : archive.entries) {
    if (entry.name != "Game.cfg") {
      continue;
    }
    std::vector<uint8_t> data;
    if (!ReadEntry(archive, entry, &data, &error)) {
      return state;
    }
    const std::string text(data.begin(), data.end());
    for (const std::string& line : BlockLines(text)) {
      if (line.rfind(kFovKey, 0) != 0) {
        continue;
      }
      const size_t space = line.find(' ');
      if (space != std::string::npos) {
        state.degrees = std::atoi(line.c_str() + space + 1);
        state.present = true;
      }
    }
    return state;
  }
  return state;
}

// ---------------------------------------------------------------------------
// Film grain
// ---------------------------------------------------------------------------

struct GrainState {
  // False when Game.cfg never mentions the setting; the dialog then shows the
  // shipped default (grain off).
  bool present = false;
  bool enabled = true;
};

// Recognises both shapes the setting can take in Game.cfg:
// `PostProcess.FilmGrainEnable = true` (plain Lua) and the applySettings
// key/value form `PostProcess.FilmGrainEnable true`.
inline bool ParseGrainLine(const std::string& raw, bool* enabled) {
  const size_t begin = raw.find_first_not_of(" \t");
  const size_t end = raw.find_last_not_of(" \t\r\n");
  if (begin == std::string::npos || end == std::string::npos || end < begin) {
    return false;
  }
  const std::string line = raw.substr(begin, end - begin + 1);
  if (line.rfind(kGrainKey, 0) != 0) {
    return false;
  }
  size_t pos = std::strlen(kGrainKey);
  if (pos >= line.size() || (line[pos] != '=' && line[pos] != ' ' && line[pos] != '\t')) {
    return false;
  }
  pos = line.find_first_not_of("= \t", pos);
  if (pos == std::string::npos) {
    return false;
  }
  const size_t stop = line.find_first_of(" \t,;", pos);
  std::string value =
      line.substr(pos, stop == std::string::npos ? std::string::npos : stop - pos);
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (value == "true" || value == "1" || value == "on") {
    *enabled = true;
    return true;
  }
  if (value == "false" || value == "0" || value == "off") {
    *enabled = false;
    return true;
  }
  return false;
}

inline GrainState ReadGrain(const std::filesystem::path& config_zz) {
  GrainState state;
  Archive archive;
  std::string error;
  if (config_zz.empty() || !LoadArchive(config_zz, &archive, &error)) {
    return state;
  }
  for (const Entry& entry : archive.entries) {
    if (entry.name != "Game.cfg") {
      continue;
    }
    std::vector<uint8_t> data;
    if (!ReadEntry(archive, entry, &data, &error)) {
      return state;
    }
    const std::string text(data.begin(), data.end());
    // Game.cfg is Lua: the last assignment wins, so keep scanning after a hit
    // (an applySettings entry earlier in the file may be overridden later).
    size_t pos = 0;
    while (pos < text.size()) {
      const size_t nl = text.find('\n', pos);
      const size_t stop = (nl == std::string::npos) ? text.size() : nl;
      bool enabled = false;
      if (ParseGrainLine(text.substr(pos, stop - pos), &enabled)) {
        state.enabled = enabled;
        state.present = true;
      }
      if (nl == std::string::npos) {
        break;
      }
      pos = nl + 1;
    }
    return state;
  }
  return state;
}

// Rewrites every grain assignment in `text`; appends one when no line sets it.
inline std::string WithGrain(const std::string& text, bool enabled) {
  const std::string value = enabled ? "true" : "false";
  std::string out;
  out.reserve(text.size() + 64);
  bool found = false;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t nl = text.find('\n', pos);
    const size_t stop = (nl == std::string::npos) ? text.size() : nl + 1;
    const std::string raw = text.substr(pos, stop - pos);
    bool current = false;
    if (ParseGrainLine(raw, &current)) {
      const size_t first = raw.find_first_not_of(" \t");
      const std::string indent =
          first == std::string::npos ? std::string() : raw.substr(0, first);
      const size_t last = raw.find_last_not_of("\r\n");
      const std::string eol = last == std::string::npos ? std::string() : raw.substr(last + 1);
      // Keep the style the file already uses: applySettings entries are
      // space-separated, Lua assignments use '='.
      const size_t after = first + std::strlen(kGrainKey);
      const size_t marker = raw.find_first_not_of(" \t", after);
      const bool lua = marker != std::string::npos && raw[marker] == '=';
      out += indent + kGrainKey + (lua ? " = " : " ") + value + eol;
      found = true;
    } else {
      out += raw;
    }
    pos = stop;
  }
  if (!found) {
    if (!out.empty() && out.back() != '\n') {
      out += '\n';
    }
    out += std::string(kGrainKey) + " = " + value + "\n";
  }
  return out;
}

// ---------------------------------------------------------------------------
// Archive rewrite
// ---------------------------------------------------------------------------

// Applies `transform` to Game.cfg and writes the whole archive back. Every
// other entry is copied byte for byte (each one's local region, so trailing
// data descriptors survive) and the result is re-parsed and diffed before the
// real file is touched. Returns true without writing when nothing changed.
inline bool WriteGameCfg(
    const std::filesystem::path& config_zz,
    const std::function<std::string(const std::string&)>& transform, std::string* error) {
  if (config_zz.empty()) {
    *error = "no config.zz path";
    return false;
  }

  Archive archive;
  if (!LoadArchive(config_zz, &archive, error)) {
    return false;
  }

  int game_cfg = -1;
  for (size_t i = 0; i < archive.entries.size(); ++i) {
    if (archive.entries[i].name == "Game.cfg") {
      game_cfg = static_cast<int>(i);
      break;
    }
  }
  if (game_cfg < 0) {
    *error = "Game.cfg is not in " + config_zz.string();
    return false;
  }

  std::vector<uint8_t> game_cfg_data;
  if (!ReadEntry(archive, archive.entries[game_cfg], &game_cfg_data, error)) {
    return false;
  }
  const std::string old_text(game_cfg_data.begin(), game_cfg_data.end());
  const std::string new_text = transform(old_text);
  if (new_text == old_text) {
    return true;  // already stored
  }

  // Copy entries in local-header order, so each one's whole region (data and
  // any trailing descriptor) moves as-is.
  std::vector<size_t> order(archive.entries.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return archive.entries[a].local_offset < archive.entries[b].local_offset;
  });

  std::vector<uint8_t> out;
  std::vector<uint32_t> new_offsets(archive.entries.size(), 0);
  const uint32_t new_crc = Crc32(reinterpret_cast<const uint8_t*>(new_text.data()), new_text.size());

  for (size_t i = 0; i < order.size(); ++i) {
    const Entry& entry = archive.entries[order[i]];
    const uint32_t new_offset = static_cast<uint32_t>(out.size());
    new_offsets[order[i]] = new_offset;

    if (order[i] == static_cast<size_t>(game_cfg)) {
      // Local header: stored, sizes known, no data descriptor.
      const uint16_t time = Rd16(&archive.bytes[entry.cd_pos + 12]);
      const uint16_t date = Rd16(&archive.bytes[entry.cd_pos + 14]);
      Wr32(out, 0x04034b50u);
      Wr16(out, 20);  // version needed
      Wr16(out, 0);   // flags
      Wr16(out, 0);   // method: stored
      Wr16(out, time);
      Wr16(out, date);
      Wr32(out, new_crc);
      Wr32(out, static_cast<uint32_t>(new_text.size()));
      Wr32(out, static_cast<uint32_t>(new_text.size()));
      Wr16(out, static_cast<uint16_t>(entry.name.size()));
      Wr16(out, 0);  // no extra field
      WrBytes(out, entry.name.data(), entry.name.size());
      WrBytes(out, new_text.data(), new_text.size());
      continue;
    }

    const size_t next_local = (i + 1 < order.size())
                                  ? archive.entries[order[i + 1]].local_offset
                                  : archive.cd_offset;
    if (next_local <= entry.local_offset || next_local > archive.bytes.size()) {
      *error = "entry " + entry.name + " has an invalid local region";
      return false;
    }
    WrBytes(out, &archive.bytes[entry.local_offset], next_local - entry.local_offset);
  }

  const uint32_t new_cd_offset = static_cast<uint32_t>(out.size());
  for (size_t i = 0; i < archive.entries.size(); ++i) {
    const Entry& entry = archive.entries[i];
    if (i == static_cast<size_t>(game_cfg)) {
      const uint16_t version_made = Rd16(&archive.bytes[entry.cd_pos + 4]);
      const uint16_t time = Rd16(&archive.bytes[entry.cd_pos + 12]);
      const uint16_t date = Rd16(&archive.bytes[entry.cd_pos + 14]);
      const uint16_t int_attr = Rd16(&archive.bytes[entry.cd_pos + 36]);
      const uint32_t ext_attr = Rd32(&archive.bytes[entry.cd_pos + 38]);
      Wr32(out, 0x02014b50u);
      Wr16(out, version_made);
      Wr16(out, 20);
      Wr16(out, 0);  // flags
      Wr16(out, 0);  // stored
      Wr16(out, time);
      Wr16(out, date);
      Wr32(out, new_crc);
      Wr32(out, static_cast<uint32_t>(new_text.size()));
      Wr32(out, static_cast<uint32_t>(new_text.size()));
      Wr16(out, static_cast<uint16_t>(entry.name.size()));
      Wr16(out, 0);
      Wr16(out, 0);
      Wr16(out, 0);  // disk start
      Wr16(out, int_attr);
      Wr32(out, ext_attr);
      Wr32(out, new_offsets[i]);
      WrBytes(out, entry.name.data(), entry.name.size());
      continue;
    }
    const size_t start = out.size();
    WrBytes(out, &archive.bytes[entry.cd_pos], entry.cd_len);
    Patch32(out, start + 42, new_offsets[i]);
  }

  const uint32_t cd_size = static_cast<uint32_t>(out.size()) - new_cd_offset;
  Wr32(out, 0x06054b50u);
  Wr16(out, 0);
  Wr16(out, 0);
  Wr16(out, static_cast<uint16_t>(archive.entries.size()));
  Wr16(out, static_cast<uint16_t>(archive.entries.size()));
  Wr32(out, cd_size);
  Wr32(out, new_cd_offset);
  Wr16(out, 0);

  // Validate before touching the real file: the archive must still parse, every
  // other entry must be unchanged, and Game.cfg must hold the requested value.
  {
    Archive check;
    check.bytes = out;
    std::string verify_error;
    if (!ParseArchive(&check, &verify_error)) {
      *error = "rewritten archive does not parse: " + verify_error;
      return false;
    }
    if (check.entries.size() != archive.entries.size()) {
      *error = "rewritten archive lost entries";
      return false;
    }
    bool found_game_cfg = false;
    for (size_t i = 0; i < check.entries.size(); ++i) {
      const Entry& entry = check.entries[i];
      if (entry.name != archive.entries[i].name) {
        *error = "rewritten archive reordered " + entry.name;
        return false;
      }
      std::vector<uint8_t> data;
      std::string read_error;
      if (!ReadEntry(check, entry, &data, &read_error)) {
        *error = "rewritten " + entry.name + " cannot be read: " + read_error;
        return false;
      }
      if (entry.name == "Game.cfg") {
        found_game_cfg = true;
        const std::string round_trip(data.begin(), data.end());
        if (round_trip != new_text) {
          *error = "rewritten Game.cfg does not match";
          return false;
        }
        continue;
      }
      std::vector<uint8_t> original;
      if (!ReadEntry(archive, archive.entries[i], &original, &read_error)) {
        *error = "original " + entry.name + " cannot be read: " + read_error;
        return false;
      }
      if (original != data) {
        *error = "rewritten " + entry.name + " changed";
        return false;
      }
    }
    if (!found_game_cfg) {
      *error = "rewritten archive has no Game.cfg";
      return false;
    }
  }

  // Back up the archive the first time it is modified, like the Python tool.
  std::error_code ec;
  const std::filesystem::path backup = config_zz.string() + ".bfbc-backup";
  if (!std::filesystem::exists(backup, ec)) {
    std::filesystem::copy_file(config_zz, backup, std::filesystem::copy_options::none, ec);
    if (ec) {
      *error = "cannot write " + backup.string() + ": " + ec.message();
      return false;
    }
  }

  const std::filesystem::path tmp = config_zz.string() + ".tmp";
  {
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (!file) {
      *error = "cannot write " + tmp.string();
      return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    file.flush();
    if (!file) {
      *error = "cannot write " + tmp.string();
      return false;
    }
  }
  std::filesystem::copy_file(tmp, config_zz, std::filesystem::copy_options::overwrite_existing, ec);
  std::filesystem::remove(tmp, ec);
  if (ec) {
    *error = "cannot replace " + config_zz.string() + ": " + ec.message();
    return false;
  }
  return true;
}

// degrees <= 0 removes the override and leaves the engine's own value.
inline bool WriteFov(const std::filesystem::path& config_zz, int degrees, std::string* error) {
  if (degrees < 0) {
    degrees = 0;
  }
  if (degrees > 0) {
    degrees = std::clamp(degrees, kFovMin, kFovMax);
  }
  return WriteGameCfg(
      config_zz,
      [degrees](const std::string& text) {
        return ReplaceBlock(text, WithFov(BlockLines(text), degrees > 0, degrees));
      },
      error);
}

inline bool WriteGrain(const std::filesystem::path& config_zz, bool enabled, std::string* error) {
  return WriteGameCfg(
      config_zz, [enabled](const std::string& text) { return WithGrain(text, enabled); }, error);
}

}  // namespace bfbc::configzip
