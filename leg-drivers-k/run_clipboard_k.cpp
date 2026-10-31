// Physical-device round acceptance driver (tester-authored, NOT a unit test).
#include <cstdio>

int RunClipboardLeg(const char* serial);

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("LEG_CLIPBOARD_RC=99 no-serial\n");
    return 99;
  }
  int rc = RunClipboardLeg(argv[1]);
  std::printf("LEG_CLIPBOARD_RC=%d\n", rc);
  return rc;
}
