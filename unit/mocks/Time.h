#ifndef KICKCAT_UNIT_MOCKS_TIME_H
#define KICKCAT_UNIT_MOCKS_TIME_H

namespace kickcat
{
    // Reset the mocked clock (now() / since_unix_epoch()) starting point to now.
    void resetMockClock();
}

#endif
