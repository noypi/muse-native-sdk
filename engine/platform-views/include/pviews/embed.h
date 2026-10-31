#pragma once

#include <cstdint>

struct HostHandle {
  std::uint64_t value = 0;
};

struct EngineHandle {
  std::uint64_t value = 0;
};

struct ViewHandle {
  std::uint64_t value = 0;
};

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct EmbedOpts {};

struct ViewSlot {
  ViewHandle view{};
  Rect bounds{};
};

enum class EmbedErrorCode {
  NONE = 0,
  EMBED_ALREADY_ATTACHED = 1,
  EMBED_BAD_HOST = 2
};

struct EngineEmbedResult {
  bool ok = false;
  EngineHandle handle{};
  EmbedErrorCode error = EmbedErrorCode::NONE;
};

struct ViewSlotResult {
  bool ok = false;
  ViewSlot slot{};
  EmbedErrorCode error = EmbedErrorCode::NONE;
};

EngineEmbedResult embedEngineInView(HostHandle host, EmbedOpts opts) noexcept;
ViewSlotResult embedViewInEngine(EngineHandle engine, ViewHandle view,
                                 Rect bounds) noexcept;
