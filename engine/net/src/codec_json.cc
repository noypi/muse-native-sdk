#include "net/codec.h"

namespace muse {
namespace net {

// JSON codec: the wire format is the canonical JSON text itself, so
// decode(encode(x)) is byte-stable by construction. Malformed input is
// rejected by the shared strict canonicalizer (no trailing commas,
// finite numbers only, depth/size caps) — never a throw.
std::string JsonCodec::encode(const std::string& json) {
  try {
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(json, &canon, &err, &field)) return "";
    return canon;
  } catch (...) {
    return "";
  }
}

DecodeResult JsonCodec::decode(std::string_view bytes) {
  try {
    if (bytes.size() > codec_detail::kMaxJsonBytes)
      return codec_detail::Fail("size-cap-exceeded");
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(bytes, &canon, &err, &field))
      return codec_detail::Fail(std::move(err), std::move(field));
    DecodeResult r;
    r.ok = true;
    r.payload = std::move(canon);
    return r;
  } catch (...) {
    return codec_detail::Fail("internal-error");
  }
}

}  // namespace net
}  // namespace muse
