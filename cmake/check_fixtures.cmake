# Fail with the fix, not with a mystery.
#
# tests/fixtures/ is gitignored: it is generated data, it needs a JDK and
# Pillow, and generating it on every build would make "cmake --build" a
# multi-second job that has nothing to do with compiling.  So it is generated
# once, explicitly - and if it has not been, say so in one line.
if(NOT EXISTS "${FIX}")
  message(FATAL_ERROR
    "test fixtures are missing.\n"
    "  Generate them once (needs a JDK and, for PNG/JPEG, Pillow):\n"
    "      cmake --build <build-dir> --target nj_fixtures\n"
    "  Two of the fixtures are downloaded from GitHub and are behind their own\n"
    "  switch, because a build should not need the network:\n"
    "      cmake -S . -B <build-dir> -DNJ_FIXTURES_FETCH_GAMES=ON\n"
    "  Then run ctest again.")
endif()
