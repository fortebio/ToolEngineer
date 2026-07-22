// Host regression check for the readCommand() buffer-overflow fix
// (src/ForteSetting.cpp). No hardware, no PlatformIO.
//
// It reproduces readCommand()'s read loop with a UART-like fake Stream and feeds a
// >2048-byte multi-chunk message (the moreMsg accumulation path, where recvLen is
// already >0 when the next readBytes runs). recvData is over-allocated with canary
// bytes past its logical 2048-byte end, so an out-of-bounds write is detected as a
// clobbered canary instead of crashing.
//
// Proves BOTH directions so the test actually discriminates:
//   FIXED (bound = remaining space)  -> stays in bounds, hits the too-long flush.
//   OLD   (fixed 1024*2 count)       -> overflows past recvData[2047].
//
// Build + run (MinGW/gcc/clang all fine):
//   g++ -O2 -std=c++17 tools/test_readcmd_overflow.cpp -o test_readcmd && ./test_readcmd
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <vector>

static const size_t SIZE = 2048; // logical recvData capacity readCommand enforces
static const size_t PAD = 512;   // canary region past the logical end (0x5A)

// Minimal Arduino Stream stand-in: hands out bytes in fixed-size chunks like a UART
// RX buffer, so one readBytes() call never returns the whole message at once.
struct FakeStream
{
    const std::vector<uint8_t> &data;
    size_t pos = 0, chunk;
    FakeStream(const std::vector<uint8_t> &d, size_t c) : data(d), chunk(c) {}
    int available() const { return (int)(data.size() - pos); }
    uint16_t readBytes(char *buf, size_t count)
    {
        size_t n = data.size() - pos;
        if (n > count) n = count;
        if (n > chunk) n = chunk; // UART: at most one chunk per call
        memcpy(buf, &data[pos], n);
        pos += n;
        return (uint16_t)n;
    }
};

// Runs readCommand()'s read loop. boundedByRemaining=true is the fix; false is the old
// bug (fixed 1024*2 count). Returns true if the canary past recvData[SIZE) was clobbered.
static bool overflowed(const std::vector<uint8_t> &msg, size_t chunk, bool boundedByRemaining, int &result)
{
    char *buf = (char *)malloc(SIZE + PAD);
    assert(buf);
    memset(buf + SIZE, 0x5A, PAD); // canary

    FakeStream port(msg, chunk);
    uint16_t recvLen = 0;
    result = 0; // 0 = still reading, -1 = too-long flush
    while (port.available() > 0)
    {
        size_t count = boundedByRemaining ? (SIZE - recvLen) : (1024 * 2);
        uint16_t len = port.readBytes(buf + recvLen, count);
        if (len == 0) break;
        recvLen += len;
        if (recvLen >= SIZE) { result = -1; recvLen = 0; break; }
    }
    buf[recvLen] = '\0'; // in-bounds: guard caps recvLen < SIZE

    bool clobbered = false;
    for (size_t i = SIZE; i < SIZE + PAD; i++)
        if ((unsigned char)buf[i] != 0x5A) { clobbered = true; break; }
    free(buf);
    return clobbered;
}

int main()
{
    // >2048 bytes, starts with '{', no '@'/'#' terminator (the moreMsg path), delivered
    // in 250-byte chunks (250 does not divide 2048, so recvLen lands where recvLen+chunk
    // straddles the end - exactly the overflow window).
    std::vector<uint8_t> msg(3000, 'x');
    msg[0] = '{';
    const size_t CHUNK = 250;

    int rFix = 0, rBug = 0;
    bool fixOverflow = overflowed(msg, CHUNK, /*boundedByRemaining=*/true, rFix);
    bool bugOverflow = overflowed(msg, CHUNK, /*boundedByRemaining=*/false, rBug);

    assert(!fixOverflow && "FIX: buffer overflowed - the remaining-space bound is broken");
    assert(rFix == -1 && "FIX: a >2048B message must hit the too-long flush");
    assert(bugOverflow && "TEST NOT DISCRIMINATING: old count=2048 did not overflow here");

    printf("FIXED : bound=remaining -> no overflow, flushed as too-long   OK\n");
    printf("OLD   : fixed 1024*2    -> overflowed recvData[2048] (canary clobbered)   OK\n");
    printf("\nPASS: readCommand() read loop stays within recvData[2048] on a 3000B multi-chunk input.\n");
    return 0;
}
