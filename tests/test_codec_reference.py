"""
Step 3 verification (corrected): the reference implementation is what the
C code MUST match. We feed crafted payloads and check that the raw values
extracted are exactly what we wrote via BitEncode.
"""
def py_get_bit_lsb(data, n):
    return (data[n >> 3] >> (n & 0x7)) & 1
def py_get_bit_msb(data, n):
    byte_idx = n >> 3
    bit_idx  = 7 - (n & 0x7)
    return (data[byte_idx] >> bit_idx) & 1
def BitExtract(data, start_bit, length, byte_order):
    if length == 0: return 0
    value = 0
    for i in range(length):
        n = start_bit + length - 1 - i
        b = py_get_bit_lsb(data, n) if byte_order == 0 else py_get_bit_msb(data, n)
        value = (value << 1) | b
    return value & 0xFFFFFFFF
def BitExtractSigned(data, start_bit, length, byte_order):
    raw = BitExtract(data, start_bit, length, byte_order)
    if length == 0 or length >= 32:
        return raw - 0x100000000 if raw & 0x80000000 else raw
    if raw & (1 << (length - 1)):
        raw |= ((~0) << length) & 0xFFFFFFFF
    return raw - 0x100000000 if raw & 0x80000000 else raw
def BitEncode(data, start_bit, length, byte_order, value):
    if length == 0: return
    for i in range(length):
        n = start_bit + i
        b = (value >> i) & 1
        if byte_order == 0:
            byte_idx, bit_idx = n >> 3, n & 0x7
        else:
            byte_idx, bit_idx = n >> 3, 7 - (n & 0x7)
        if b:
            data[byte_idx] |=  (1 << bit_idx)
        else:
            data[byte_idx] &= ~(1 << bit_idx)

# Round-trip tests: encode raw value, decode raw value, expect identity
cases = [
    # name, start_bit, length, byte_order, raw_value
    ("MMI_Second 6bit Intel",     5,   6, 0, 42),
    ("MMI_Hour 5bit Intel",       20,  5, 0, 23),
    ("MMI_Year 6bit Intel",       45,  6, 0, 63),
    ("EMS_RPM 16bit Intel",       23, 16, 0, 12345),
    ("EMS_RPM 16bit Intel max",   23, 16, 0, 65535),
    ("EMS_RPM 16bit Intel 0",     23, 16, 0, 0),
    ("IPU_IsgTq 16bit signed",    15, 16, 0, 22220),
    ("IPU_IsgTq signed negative", 15, 16, 0, 0),  # raw 0 -> physical -1200; raw is unsigned here
    ("ESC_VehicleSpeed 13bit",    15, 13, 0, 8191),  # max
    ("ESC_VehicleSpeed 13bit 0",  15, 13, 0, 0),
    ("IPK_vDisplay 13bit Motor",  47, 13, 1, 100),
    ("IPK_vDisplay 13bit Motor 0",47, 13, 1, 0),
    ("IPK_FuelLevelSts 8bit Intel", 15, 8, 0, 200),
    ("MMI_SkinMode 2bit",         33,  2, 0, 3),
    ("MMI_OdometerClear 1bit",    19,  1, 0, 1),
    ("1bit at byte boundary (8)", 8,  1, 0, 1),
    ("1bit at byte boundary (15)",15, 1, 0, 1),
    ("1bit Motorola at start_bit 7", 7, 1, 1, 1),
    ("32-bit full-width Intel",    0, 32, 0, 0xDEADBEEF),
    ("32-bit full-width Motorola", 0, 32, 1, 0xDEADBEEF),
    ("Cross-byte Motorola 16bit", 7,  16, 1, 0xABCD),
    ("GPS_elevation 18bit",       7,  18, 0, 0x12345),
]

print(f"{'TEST':35s} {'RAW':10s} {'DECODED':10s} {'STATUS'}")
print("-" * 80)
ok = True
for name, sb, length, bo, raw in cases:
    data = [0]*8
    BitEncode(data, sb, length, bo, raw)
    dec = BitExtract(data, sb, length, bo)
    status = "OK" if dec == raw else f"FAIL diff={(dec^raw):X}"
    if dec != raw: ok = False
    print(f"{name:35s} 0x{raw:08X} 0x{dec:08X} {status}")

print()
print("ALL ROUND-TRIPS PASS" if ok else "SOME TESTS FAILED")
