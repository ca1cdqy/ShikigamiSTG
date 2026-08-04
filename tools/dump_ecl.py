#!/usr/bin/env python3
"""
dump_ecl.py - TH06 ECL binary disassembler

Binary layout (version 6):
  Header:
    uint16 subCount
    uint16 timelineCount
  Offset table (uint32 each):
    [3 timeline offsets][subCount sub offsets]
  Instruction record (12-byte header):
    uint32 time
    uint16 id
    uint16 size
    uint16 rankMask
    uint16 paramMask
    [param data: size - 12 bytes]
  End marker: time == 0xFFFFFFFF  or  size == 0
"""

import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Instruction format table (from ecl_parser.cpp / thecl06.c)
# Type codes:
#   S  = int32    (4 bytes)   - shown as int; also shown as float when ambiguous
#   s  = int16    (2 bytes)
#   U  = uint32   (4 bytes)
#   u  = uint16   (2 bytes)
#   f  = float32  (4 bytes)
#   C  = uint8    (1 byte)
#   z  = string   (34 bytes in TH06)
#   n  = int16  sub index
#   N  = int32  sub index
#   o  = int32  jump offset (relative to instruction start)
#   t  = int32  time target
#   T  = int32  timeline index
# ---------------------------------------------------------------------------
TH06_FORMATS: dict[int, str] = {
    0:   "",
    1:   "S",
    2:   "to",
    3:   "toS",
    4:   "SS",
    5:   "Sf",
    6:   "SU",
    7:   "SUS",
    8:   "Sf",
    9:   "Sff",
    10:  "S",
    11:  "S",
    12:  "S",
    13:  "SSS",
    14:  "SSS",
    15:  "SSS",
    16:  "SSS",
    17:  "SSS",
    18:  "S",
    19:  "S",
    20:  "Sff",
    21:  "Sff",
    22:  "Sff",
    23:  "Sff",
    24:  "Sff",
    25:  "Sffff",
    26:  "S",
    27:  "SS",
    28:  "ff",
    29:  "to",
    30:  "to",
    31:  "to",
    32:  "to",
    33:  "to",
    34:  "to",
    35:  "NSf",
    36:  "",
    37:  "NSfSS",
    38:  "NSfSS",
    39:  "NSfSS",
    40:  "NSfSS",
    41:  "NSfSS",
    42:  "NSfSS",
    43:  "fff",
    44:  "fff",
    45:  "ff",
    46:  "f",
    47:  "f",
    48:  "f",
    49:  "ff",
    50:  "ff",
    51:  "ff",
    52:  "Sff",
    53:  "Sff",
    54:  "Sff",
    55:  "Sff",
    56:  "Sfff",
    57:  "Sfff",
    58:  "Sfff",
    59:  "Sfff",
    60:  "Sfff",
    61:  "S",
    62:  "S",
    63:  "S",
    64:  "S",
    65:  "ffff",
    66:  "",
    67:  "ssSSffffS",
    68:  "ssSSffffS",
    69:  "ssSSffffS",
    70:  "ssSSffffS",
    71:  "ssSSffffS",
    72:  "ssSSffffS",
    73:  "ssSSffffS",
    74:  "ssSSffffS",
    75:  "ssSSffffS",
    76:  "S",
    77:  "S",
    78:  "",
    79:  "",
    80:  "",
    81:  "fff",
    82:  "SSSSffff",
    83:  "",
    84:  "S",
    85:  "ssffffffSSSSSS",
    86:  "ssffffffSSSSSS",
    87:  "S",
    88:  "Sf",
    89:  "Sf",
    90:  "Sfff",
    91:  "S",
    92:  "S",
    93:  "ssz",
    94:  "",
    95:  "NfffssS",
    96:  "",
    97:  "S",
    98:  "ssssS",
    99:  "SS",
    100: "S",
    101: "S",
    102: "Sffff",
    103: "fff",
    104: "S",
    105: "S",
    106: "S",
    107: "S",
    108: "N",
    109: "NS",
    110: "S",
    111: "S",
    112: "S",
    113: "S",
    114: "N",
    115: "S",
    116: "N",
    117: "S",
    118: "SUC",
    119: "S",
    120: "S",
    121: "SS",
    122: "S",
    123: "S",
    124: "S",
    125: "",
    126: "S",
    127: "S",
    128: "S",
    129: "SS",
    130: "S",
    131: "ffSSSS",
    132: "S",
    133: "",
    134: "",
    135: "S",
}
