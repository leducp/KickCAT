#include <cstdio>

namespace
{
    // ctest reads the test binary's stdout through a pipe, so the C runtime block-buffers it. A run
    // that hangs or dies abnormally never flushes, and the whole suite's output is lost: the log
    // then shows nothing at all, not even the test that stopped. Unbuffered from before main so the
    // last line written is always the last line reached.
    [[maybe_unused]] int const unbuffered_stdout = std::setvbuf(stdout, nullptr, _IONBF, 0);
}
