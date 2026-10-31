// Physical-device round acceptance driver (tester-authored, NOT a unit test).
// Calls the production leg entry with the argv serial and reports the rc.
// On physical serial KDSBB22429250346 the expected result is fail-fast 12
// (gate constant is the emulator serial 127.0.0.1:6569).
#include <cstdio>

int RunCompositionLeg(const char* serial);

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("LEG_COMPOSITION_RC=99 no-serial\n");
    return 99;
  }
  int rc = RunCompositionLeg(argv[1]);
  std::printf("LEG_COMPOSITION_RC=%d\n", rc);
  return rc;
}
