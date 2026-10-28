// Acceptance driver for fr-motion-011-fr-motion-012-fr-motion-013
// (FR-MOTION-011 shared elements, FR-MOTION-012 large/posture motion,
//  FR-MOTION-013 M3 motion tokens).
//
// Standalone end-to-end driver (NOT a unit test): exercises the public motion
// APIs cross-module the way an embedder would and asserts each AC.
// Compile: g++ -std=c++17 -I engine/motion/include acceptance/motion_acceptance_011_012_013.cpp -L engine/motion/build -lmotion -o /tmp/motion_acc_011_012_013
// Run from _run: /tmp/motion_acc_011_012_013
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "motion/graph.h"
#include "motion/implicit.h"
#include "motion/large_motion.h"
#include "motion/layout_adapter.h"
#include "motion/m3_tokens.h"
#include "motion/shared_element.h"
#include "motion/spring_class.h"

static int g_fail = 0;
#define CHECK(cond, name)                                              \
  do {                                                                 \
    if (cond) {                                                        \
      std::printf("PASS %s\n", name);                                  \
    } else {                                                           \
      std::printf("FAIL %s\n", name);                                  \
      g_fail++;                                                        \
    }                                                                  \
  } while (0)

static bool near(float a, float b, float eps) {
  return std::fabs(a - b) <= eps;
}

static motion::Box box(float x, float y, float w, float h) {
  motion::Box b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

static bool boxNear(const motion::Box& a, const motion::Box& b, float eps) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps) &&
         near(a.w, b.w, eps) && near(a.h, b.h, eps);
}

static float boxDist(const motion::Box& a, const motion::Box& b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  float dw = a.w - b.w;
  float dh = a.h - b.h;
  return std::sqrt(dx * dx + dy * dy + dw * dw + dh * dh);
}

static motion::SharedAnchor anchor(const std::string& id,
                                   const std::string& route, int depth,
                                   const motion::Box& b, float radius) {
  motion::SharedAnchor a;
  a.elementId = id;
  a.route = route;
  a.depth = depth;
  a.box = b;
  a.cornerRadius = radius;
  return a;
}

static motion::TransitionSpec springSpec(float stiffness, float damping) {
  motion::TransitionSpec s;
  s.spring.stiffness = stiffness;
  s.spring.dampingRatio = damping;
  s.delayMs = 0.0f;
  s.staggerMs = 0.0f;
  return s;
}

static bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

int main() {
  // ---- AC-MOTION-034 (stable identity) ----
  {
    // Reordered trees: match follows the id, not the tree position.
    std::vector<motion::SharedAnchor> from = {
        anchor("a", "/home", 0, box(0, 0, 10, 10), 0),
        anchor("b", "/home", 1, box(10, 10, 20, 20), 2),
        anchor("c", "/home", 1, box(30, 30, 40, 40), 4),
    };
    std::vector<motion::SharedAnchor> to = {
        anchor("c", "/detail", 1, box(300, 300, 44, 44), 5),
        anchor("a", "/detail", 0, box(0, 0, 12, 12), 1),
        anchor("b", "/detail", 2, box(100, 100, 22, 22), 3),
    };
    const motion::SharedAnchor* got =
        motion::motion_matchShared(from, to, "b");
    CHECK(got != nullptr && got == &to[2] && got->depth == 2 &&
              near(got->box.x, 100.0f, 1e-5f),
          "AC-034 reordered trees match by id");
    // Nested depths: same id at different depths/routes still matches.
    std::vector<motion::SharedAnchor> from2 = {
        anchor("hero", "/home", 0, box(0, 0, 50, 50), 0),
    };
    std::vector<motion::SharedAnchor> to2 = {
        anchor("hero", "/home/detail", 3, box(5, 5, 60, 60), 8),
    };
    const motion::SharedAnchor* got2 =
        motion::motion_matchShared(from2, to2, "hero");
    CHECK(got2 != nullptr && got2 == &to2[0] &&
              got2->route == "/home/detail" && got2->depth == 3,
          "AC-034 nested depths match returns to-record");
    // Empty-from deep-link: source never shown, still matches.
    std::vector<motion::SharedAnchor> empty;
    std::vector<motion::SharedAnchor> to3 = {
        anchor("hero", "/detail", 0, box(7, 8, 90, 90), 6),
    };
    const motion::SharedAnchor* got3 =
        motion::motion_matchShared(empty, to3, "hero");
    CHECK(got3 != nullptr && got3 == &to3[0],
          "AC-034 empty-from deep-link matches");
    // From-present-but-absent: non-empty from lacking the id -> nullptr
    // even though `to` holds it.
    std::vector<motion::SharedAnchor> from4 = {
        anchor("other", "/home", 0, box(0, 0, 10, 10), 0),
    };
    CHECK(motion::motion_matchShared(from4, to3, "hero") == nullptr,
          "AC-034 from-present-but-absent returns nullptr");
  }

  // ---- AC-MOTION-035 (clip + corner-radius + reversal) ----
  {
    motion::SharedAnchor from = anchor("hero", "/list", 0,
                                       box(10, 20, 100, 50), 0);
    from.clip = box(0, 0, 50, 25);
    motion::SharedAnchor to = anchor("hero", "/detail", 0,
                                     box(30, 60, 200, 90), 12);
    to.clip = box(10, 20, 150, 75);
    motion::SharedFrame f0 = motion::motion_interpolateShared(from, to, 0.0f);
    CHECK(boxNear(f0.box, box(10, 20, 100, 50), 1e-5f) &&
              boxNear(f0.clip, box(0, 0, 50, 25), 1e-5f) &&
              near(f0.cornerRadius, 0.0f, 1e-5f),
          "AC-035 clip/corner t=0 returns from");
    motion::SharedFrame f1 = motion::motion_interpolateShared(from, to, 1.0f);
    CHECK(boxNear(f1.box, box(30, 60, 200, 90), 1e-5f) &&
              boxNear(f1.clip, box(10, 20, 150, 75), 1e-5f) &&
              near(f1.cornerRadius, 12.0f, 1e-5f),
          "AC-035 clip/corner t=1 returns to");
    motion::SharedFrame fm = motion::motion_interpolateShared(from, to, 0.5f);
    CHECK(boxNear(fm.box, box(20, 40, 150, 70), 1e-5f) &&
              boxNear(fm.clip, box(5, 10, 100, 50), 1e-5f) &&
              near(fm.cornerRadius, 6.0f, 1e-5f),
          "AC-035 clip/corner t=0.5 lerps");
    // Reversal continuity: drive 0 -> 0.6 -> 0.2 with no snap.
    motion::motion_resetSharedDrive();
    motion::Graph g;
    motion::SharedAnchor rfrom =
        anchor("hero", "/list", 0, box(0, 0, 100, 100), 4);
    motion::SharedAnchor rto =
        anchor("hero", "/detail", 0, box(200, 400, 300, 500), 16);
    motion::motion_sharedTransition(g, rfrom, rto, springSpec(180.0f, 1.0f));
    const float total = boxDist(rfrom.box, rto.box);
    const float prog[] = {0.0f, 0.6f, 0.2f};
    float prevT = motion::motion_driveShared(prog[0]);
    motion::SharedFrame prev =
        motion::motion_interpolateShared(rfrom, rto, prevT);
    bool cont = (prevT >= 0.0f && prevT <= 1.0f);
    for (int i = 1; i < 3 && cont; ++i) {
      float t = motion::motion_driveShared(prog[i]);
      if (t < 0.0f || t > 1.0f) {
        cont = false;
        break;
      }
      motion::SharedFrame f = motion::motion_interpolateShared(rfrom, rto, t);
      if (boxDist(prev.box, f.box) > std::fabs(t - prevT) * total + 1e-3f)
        cont = false;
      prevT = t;
      prev = f;
    }
    CHECK(cont, "AC-035 drive 0->0.6->0.2 continuous");
    motion::motion_resetSharedDrive();
  }

  // ---- AC-MOTION-036 (platform-view surface rects) ----
  {
    std::vector<motion::SharedAnchor> list = {
        anchor("video", "/list", 0, box(0, 0, 100, 100), 4),
    };
    motion::SharedAnchor pv = motion::motion_pviewAnchor(
        "video", "/player", box(200, 400, 300, 500), 16);
    CHECK(pv.elementId == "video" && pv.route == "/player" &&
              boxNear(pv.box, box(200, 400, 300, 500), 1e-5f) &&
              near(pv.cornerRadius, 16.0f, 1e-5f),
          "AC-036 pview anchor wraps surface rect");
    // list -> pview match.
    std::vector<motion::SharedAnchor> pvSide = {pv};
    const motion::SharedAnchor* m1 =
        motion::motion_matchShared(list, pvSide, "video");
    CHECK(m1 != nullptr && m1 == &pvSide[0],
          "AC-036 list->pview matches");
    // pview -> list match (reverse direction).
    const motion::SharedAnchor* m2 =
        motion::motion_matchShared(pvSide, list, "video");
    CHECK(m2 != nullptr && m2 == &list[0],
          "AC-036 pview->list matches");
    // Interpolation between list box and pview surface rect.
    motion::SharedFrame mid =
        motion::motion_interpolateShared(list[0], pv, 0.5f);
    CHECK(boxNear(mid.box, box(100, 200, 200, 300), 1e-5f) &&
              near(mid.cornerRadius, 10.0f, 1e-5f),
          "AC-036 list<->pview interpolation");
  }

  // ---- AC-MOTION-038/039/040 (hinge / size / container) ----
  {
    // FULL occlusion: crossing input must come back non-crossing.
    const float kHingeX = 500.0f;
    auto onOneSide = [&](const motion::Box& b) {
      return (b.x + b.w) <= kHingeX + 1e-5f || b.x >= kHingeX - 1e-5f;
    };
    bool hingeOk = true;
    for (motion::Posture p :
         {motion::Posture::Flat, motion::Posture::Tabletop,
          motion::Posture::HalfOpened, motion::Posture::Folded}) {
      motion::PaneMove m = motion::motion_paneMove(
          p, motion::HingeOcclusion::Full, box(100, 100, 200, 300),
          box(600, 100, 200, 300));
      if (m.crossHinge || !onOneSide(m.fromPane) || !onOneSide(m.toPane))
        hingeOk = false;
    }
    CHECK(hingeOk, "AC-038 FULL never spans hinge (all postures)");
    // Monotonic scaling within bounds.
    const float base = 300.0f, lo = 100.0f, hi = 600.0f;
    float c = motion::motion_scaledDuration(base, motion::SizeClass::Compact,
                                            lo, hi);
    float m = motion::motion_scaledDuration(base, motion::SizeClass::Medium,
                                            lo, hi);
    float e = motion::motion_scaledDuration(base, motion::SizeClass::Expanded,
                                            lo, hi);
    CHECK(c <= m + 1e-3f && m <= e + 1e-3f && c >= lo - 1e-3f &&
              e <= hi + 1e-3f,
          "AC-039 monotonic scaling in bounds");
    // Single container box; children untouched.
    std::vector<motion::Box> kids = {box(1, 2, 3, 4), box(5, 6, 7, 8),
                                     box(9, 10, 11, 12)};
    std::vector<motion::Box> kidsBefore = kids;
    motion::Box out = motion::motion_containerTransform(
        kids, box(10, 20, 100, 50), box(30, 60, 200, 90), 0.5f);
    bool kidsSame = (kids.size() == kidsBefore.size());
    for (size_t i = 0; kidsSame && i < kids.size(); ++i)
      kidsSame = boxNear(kids[i], kidsBefore[i], 1e-5f);
    CHECK(boxNear(out, box(20, 40, 150, 70), 1e-5f) && kidsSame,
          "AC-040 single container box, children untouched");
  }

  // ---- AC-MOTION-042/043/044 (tokens) ----
  {
    std::string error;
    motion::M3TokenSet set = motion::motion_loadM3Tokens(
        "engine/motion/tokens/m3.json", &error);
    bool dims = error.empty() && !set.version.empty();
    const char* durations[] = {"short1", "short2", "short3", "short4",
                               "medium1", "medium2", "medium3", "medium4",
                               "long1", "long2", "long3", "long4"};
    for (const char* k : durations) {
      auto it = set.durationsMs.find(k);
      if (it == set.durationsMs.end() || !(it->second > 0.0f)) dims = false;
    }
    const char* easings[] = {"standard", "emphasized", "accelerate",
                             "decelerate"};
    for (const char* k : easings) {
      auto it = set.easings.find(k);
      if (it == set.easings.end() || it->second.empty()) dims = false;
    }
    for (const char* k : {"position", "size", "shape"}) {
      auto it = set.spatialSprings.find(k);
      if (it == set.spatialSprings.end() || !(it->second.stiffness > 0.0f))
        dims = false;
    }
    for (const char* k : {"colour", "opacity"}) {
      auto it = set.effectsSprings.find(k);
      if (it == set.effectsSprings.end() || !(it->second.stiffness > 0.0f))
        dims = false;
    }
    // Constants resolve through the loaded maps (names, not literals).
    dims = dims && set.durationsMs.count(motion::kM3DurationShort1) > 0 &&
           set.easings.count(motion::kM3EasingStandard) > 0 &&
           set.spatialSprings.count(motion::kM3SpatialPosition) > 0 &&
           set.effectsSprings.count(motion::kM3EffectsColour) > 0;
    CHECK(dims, "AC-042 committed m3.json: 4 dimensions + version");
    // AC-043: spring-class enforcement both directions.
    std::string err;
    bool classOk =
        motion::motion_checkSpringClass(motion::AnimProperty::Position,
                                        "position", &err) &&
        motion::motion_checkSpringClass(motion::AnimProperty::Opacity,
                                        "opacity", &err);
    std::string err2;
    bool wrongGeo = motion::motion_checkSpringClass(
        motion::AnimProperty::Position, "colour", &err2);
    classOk = classOk && !wrongGeo && contains(err2, "Position") &&
              contains(err2, "colour");
    std::string err3;
    bool wrongFx = motion::motion_checkSpringClass(
        motion::AnimProperty::Colour, "position", &err3);
    classOk = classOk && !wrongFx && contains(err3, "Colour") &&
              contains(err3, "position");
    CHECK(classOk, "AC-043 spring-class enforcement + naming errors");
    // AC-044: version pinned; missing version field -> error naming version.
    CHECK(!set.version.empty(), "AC-044 version pinned non-empty");
    {
      std::ofstream fx("/tmp/motion_acc_nover.json");
      fx << "{\"durationsMs\": {\"short1\": 50}}";
      fx.close();
      std::string e2;
      motion::M3TokenSet s2 =
          motion::motion_loadM3Tokens("/tmp/motion_acc_nover.json", &e2);
      CHECK(!e2.empty() && contains(e2, "version") && s2.version.empty(),
            "AC-044 missing-field error names version");
    }
    // Security: 1e999 (double +inf) rejected. NOTE: 1e300 is intentionally
    // NOT asserted (accepted INFO residual: finite double, float-inf).
    {
      std::ifstream in("engine/motion/tokens/m3.json");
      std::string json((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
      std::string needle = "\"short1\": 50";
      std::string::size_type pos = json.find(needle);
      bool ok = (pos != std::string::npos);
      if (ok) {
        json.replace(pos, needle.size(), "\"short1\": 1e999");
        std::ofstream fx("/tmp/motion_acc_huge.json");
        fx << json;
        fx.close();
        std::string e3;
        motion::M3TokenSet s3 =
            motion::motion_loadM3Tokens("/tmp/motion_acc_huge.json", &e3);
        ok = !e3.empty() && contains(e3, "durationsMs.short1") &&
             s3.durationsMs.empty();
      }
      CHECK(ok, "AC-044 1e999 rejected fail-closed");
    }
  }

  if (g_fail == 0)
    std::printf("\nACCEPTANCE 011/012/013 ALL PASS\n");
  else
    std::printf("\nACCEPTANCE 011/012/013 %d FAILURES\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
