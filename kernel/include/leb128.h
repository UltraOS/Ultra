#pragma once

#define LEB128_HAS_NEXT_BYTE (1 << 7)
#define LEB128_BITS_PER_BYTE 7
#define LEB128_MAX_PER_BYTE ((1 << LEB128_BITS_PER_BYTE) - 1)
