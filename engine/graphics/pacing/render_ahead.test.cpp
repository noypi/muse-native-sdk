// T2 hermetic unit test — bounded render-ahead queue guard (FR-GRAPHICS-007, AC-GRAPHICS-018).
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cstdio>

#include "engine/graphics/pacing/render_ahead.h"

int main() {
  using pacing::Status;

  // 1. Empty queue admits all requested up to maxDepth.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(0, 3, 2);
    assert(v.status == Status::kOk);
    assert(v.admitted == 2);
    assert(v.rejected == 0);
    printf("PASS empty-queue-admits-requested\n");
  }
  // 2. Partially full queue admits only the remaining headroom.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(2, 3, 2);
    assert(v.status == Status::kOk);
    assert(v.admitted == 1);
    assert(v.rejected == 1);
    printf("PASS partial-queue-admits-headroom\n");
  }
  // 3. Over-full queue admits zero and counts all as rejected.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(3, 3, 2);
    assert(v.status == Status::kOk);
    assert(v.admitted == 0);
    assert(v.rejected == 2);
    printf("PASS full-queue-admits-zero\n");
  }
  // 4. Never admits more than maxDepth total (queuedDepth + admitted <= maxDepth).
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(1, 4, 10);
    assert(v.status == Status::kOk);
    assert(v.admitted == 3);
    assert(v.rejected == 7);
    assert(1 + v.admitted <= 4);
    printf("PASS bound-queued-plus-admitted\n");
  }
  // 5. maxDepth=1 admits at most one.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(0, 1, 5);
    assert(v.status == Status::kOk);
    assert(v.admitted == 1);
    assert(v.rejected == 4);
    printf("PASS depth-one-admits-one\n");
  }
  // 6. Queued depth beyond max clamps to zero admitted (no negative headroom).
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(9, 3, 2);
    assert(v.status == Status::kOk);
    assert(v.admitted == 0);
    assert(v.rejected == 2);
    printf("PASS overfull-clamps-zero\n");
  }
  // 7. Security: negative queuedDepth yields kInvalidArg with zeros.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(-1, 3, 2);
    assert(v.status == Status::kInvalidArg);
    assert(v.admitted == 0);
    assert(v.rejected == 0);
    printf("PASS negative-queued-invalid-arg\n");
  }
  // 8. Security: maxDepth<=0 yields kInvalidArg with zeros.
  {
    pacing::RenderAheadVerdict v0 = pacing::tryQueueFrame(0, 0, 2);
    assert(v0.status == Status::kInvalidArg);
    assert(v0.admitted == 0 && v0.rejected == 0);
    pacing::RenderAheadVerdict vneg = pacing::tryQueueFrame(0, -2, 2);
    assert(vneg.status == Status::kInvalidArg);
    assert(vneg.admitted == 0 && vneg.rejected == 0);
    printf("PASS nonpositive-maxdepth-invalid-arg\n");
  }
  // 9. Security: negative framesRequested yields kInvalidArg with zeros.
  {
    pacing::RenderAheadVerdict v = pacing::tryQueueFrame(0, 3, -1);
    assert(v.status == Status::kInvalidArg);
    assert(v.admitted == 0);
    assert(v.rejected == 0);
    printf("PASS negative-requested-invalid-arg\n");
  }
  printf("ALL T2 TESTS PASSED\n");
  return 0;
}
