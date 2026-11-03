// FR-GRAPHICS-018 T3: shipped TU for the CPU reference vocabulary.
// Includes the hermetic header plus runtime/renderer.h and pins the 9 generic
// kinds onto EXISTING ::runtime::PrimitiveKind at compile time. No present-path
// behavior changes: this TU only maps vocabulary kinds, never touches the
// presenter/renderer frame path.
#include "runtime/game_vocab.h"

#include "runtime/renderer.h"

namespace game_vocab {

::runtime::PrimitiveKind ToPrimitiveKind(GameKind kind) {
  switch (kind) {
    case GameKind::kSprite: return ::runtime::PrimitiveKind::Image;
    case GameKind::kSpriteBatch: return ::runtime::PrimitiveKind::Image;
    case GameKind::kTileGrid: return ::runtime::PrimitiveKind::Rect;
    case GameKind::kPolyline: return ::runtime::PrimitiveKind::CircleArc;
    case GameKind::kEllipse: return ::runtime::PrimitiveKind::CircleArc;
    case GameKind::kTextRun: return ::runtime::PrimitiveKind::ShapedText;
    case GameKind::kClipRect: return ::runtime::PrimitiveKind::Rect;
    case GameKind::kTransformMark: return ::runtime::PrimitiveKind::Rect;
    case GameKind::kOpacityGroup: return ::runtime::PrimitiveKind::StateLayer;
    case GameKind::kRect: return ::runtime::PrimitiveKind::Rect;
    case GameKind::kRoundedRect: return ::runtime::PrimitiveKind::RoundedRect;
    case GameKind::kShadow: return ::runtime::PrimitiveKind::ElevationShadow;
    case GameKind::kScrim: return ::runtime::PrimitiveKind::OverlayScrim;
    case GameKind::kImage: return ::runtime::PrimitiveKind::Image;
    case GameKind::kIcon: return ::runtime::PrimitiveKind::VectorIcon;
    default: return ::runtime::PrimitiveKind::Rect;  // Unreached: staging
                                                   // rejects these first.
  }
}

}  // namespace game_vocab
