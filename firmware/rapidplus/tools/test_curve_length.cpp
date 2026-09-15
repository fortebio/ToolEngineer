// Host check for the /reviewlast curve-length scan (src/ForteSetting.cpp). No hardware.
//
// A stored EEPROM record carries no length of its own. The review path must derive the run
// length by SCANNING the record (last plausible slot-0 round), NOT by trusting the current
// amplification_time - otherwise /curve reads past the real data into garbage (config now
// larger) or truncates the run (config now smaller). This mirrors the firmware scan and
// proves it returns the real length regardless of what amplification_time happens to be.
//
// Build + run:  g++ -O2 -std=c++17 tools/test_curve_length.cpp -o test_curvelen && ./test_curvelen
#include <cstdint>
#include <cstdio>
#include <cassert>

// Mirror of the firmware scan over slot 0 of sensor67Value[10][130].
static uint8_t scanLen(const uint16_t row[130])
{
    uint8_t len = 0;
    for (uint8_t j = 0; j < 130; j++)
    {
        uint16_t v = row[j];
        if (v > 10 && v < 60000)
            len = j + 1;
    }
    return len;
}

// A stored row: n rounds of real baseline (~150+), then `tail` for the unwritten rounds.
// Real device tails: 0 (run-end zero-inits the staging buffer) or 0xFFFF (virgin EEPROM).
static void fill(uint16_t row[130], int n, uint16_t tail)
{
    for (int j = 0; j < 130; j++)
        row[j] = (j < n) ? (uint16_t)(150 + j) : tail;
}

int main()
{
    uint16_t row[130];

    fill(row, 120, 0x0000);
    assert(scanLen(row) == 120); // finished 120-round run, zeroed tail
    fill(row, 40, 0xFFFF);
    assert(scanLen(row) == 40); // 40-round run, virgin-EEPROM tail
    fill(row, 130, 0x0000);
    assert(scanLen(row) == 130); // full-length run
    fill(row, 1, 0x0000);
    assert(scanLen(row) == 1); // one round

    // The whole point: run was 40 rounds but amplification_time is now 120. The OLD code
    // set length = 120 and /curve drew 80 rounds of garbage; the scan must return 40.
    fill(row, 40, 0x0000);
    assert(scanLen(row) == 40 && scanLen(row) != 120);
    fill(row, 40, 0xFFFF);
    assert(scanLen(row) == 40 && scanLen(row) != 120);

    printf("PASS: curve-length scan returns the real stored run length, not amplification_time\n");
    return 0;
}
