#include "net/codec.h"

namespace muse {
namespace net {

// CBOR codec (dep-free): wire = "CB01" magic + canonical JSON body. The
// magic keeps the framing explicit on the wire while the shared strict
// canonicalizer enforces validation (required shape, finite-number
// ranges, depth/size caps). Malformed input → structured error, never a
// throw; encode/decode are byte-stable by construction.
std::string CborCodec::encode(const std::string& json) {
  try {
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(json, &canon, &err, &field)) return "";
    return codec_detail::Frame(CodecId::kCbor, canon);
  } catch (...) {
    return "";
  }
}

DecodeResult CborCodec::decode(std::string_view bytes) {
  try {
    if (bytes.size() > codec_detail::kMaxJsonBytes)
      return codec_detail::Fail("size-cap-exceeded");
    std::string_view body;
    std::string uerr;
    if (!codec_detail::Unframe(CodecId::kCbor, bytes, &body, &uerr))
      return codec_detail::Fail(std::move(uerr));
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(body, &canon, &err, &field))
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
