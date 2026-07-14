"""Mock test for RTI caller-private slot pool.

The C reference impl in app/rti/rti.c maintains a 64-slot pool.
This test mocks the same algorithm in Python so we can validate
the algorithm logic before / without flashing the chip.

Algorithm contract:
  - RTI_OpenSlot(p) returns first free slot bound to period p.
  - RTI_SlotElapsed(&slot) checks if period elapsed since last fire.
  - Each slot is independent (different last timestamp).
  - Pool size = 64; pool full -> open returns invalid handle.

Mocked monotonic clock: tests advance by setting `mock_now`.
"""

POOL_SIZE = 64


class Slot:
    __slots__ = ("period", "last_ms", "inited")

    def __init__(self):
        self.period = 0
        self.last_ms = 0
        self.inited = False


_pool = [Slot() for _ in range(POOL_SIZE)]
mock_now = [0]


def reset():
    """Reset pool and clock. Call at start of each test."""
    global _pool
    _pool = [Slot() for _ in range(POOL_SIZE)]
    mock_now[0] = 0


def open(period_ms):
    """Mirror of RTI_OpenSlot: return slot index (>=0) or -1 if full."""
    for i, s in enumerate(_pool):
        if not s.inited:
            s.inited = True
            s.period = period_ms
            s.last_ms = mock_now[0]
            return i
    return -1


def elapsed(handle, now_ms=None):
    """Mirror of RTI_SlotElapsed: True if period elapsed since last fire."""
    if now_ms is None:
        now_ms = mock_now[0]
    if handle < 0 or handle >= POOL_SIZE:
        return False
    s = _pool[handle]
    if not s.inited:
        return False
    if (now_ms - s.last_ms) >= s.period:
        s.last_ms = now_ms
        return True
    return False


# -------------------------------------------------------------------------
# Test 1: two slots, same period, independent timelines
# -------------------------------------------------------------------------
def test_two_slots_same_period_independent():
    reset()
    h_a = open(100)
    h_b = open(100)
    assert h_a != h_b, "must allocate distinct slot indices"

    mock_now[0] = 100
    assert elapsed(h_a) is True,  "slot A should fire at t=100"
    assert elapsed(h_b) is True,  "slot B should fire at t=100 (own timeline)"

    mock_now[0] = 150
    assert elapsed(h_a) is False, "slot A: 50ms since last fire (<100)"
    assert elapsed(h_b) is False, "slot B: 50ms since last fire (<100)"

    mock_now[0] = 200
    assert elapsed(h_a) is True,  "slot A fires again at t=200"
    assert elapsed(h_b) is True,  "slot B fires again at t=200"


# -------------------------------------------------------------------------
# Test 2: pool full returns -1 (mirrors RTI_OpenSlot invalid handle)
# -------------------------------------------------------------------------
def test_pool_full_returns_invalid():
    reset()
    handles = [open(10) for _ in range(POOL_SIZE)]
    assert all(h >= 0 for h in handles), "all 64 slots must allocate"
    extra = open(10)
    assert extra == -1, "65th open must return invalid (-1)"


# -------------------------------------------------------------------------
# Test 3: elapsed on invalid handle is False (no crash)
# -------------------------------------------------------------------------
def test_elapsed_invalid_handle_returns_false():
    reset()
    assert elapsed(-1) is False
    assert elapsed(POOL_SIZE) is False
    assert elapsed(999) is False


# -------------------------------------------------------------------------
# Test 4: 8 modules all want 100ms — every one must fire (proves no collision)
# -------------------------------------------------------------------------
def test_eight_modules_100ms_all_fire():
    """Regression for the old RTI_IsElapsed slot-per-period bug:
    with 8 callers sharing RTI_100MS, every caller must fire on its
    own 100ms tick. With the old shared-slot impl only the LAST
    caller (slot 1 of 8) would actually fire.
    """
    reset()
    handles = [open(100) for _ in range(8)]
    assert len(set(handles)) == 8, "8 distinct slots required"

    # Run for 500ms; each module should fire 5 times
    fire_count = [0] * 8
    mock_now[0] = 100
    for tick in range(1, 6):  # ticks at 100, 200, 300, 400, 500
        for i, h in enumerate(handles):
            if elapsed(h):
                fire_count[i] += 1
        mock_now[0] += 100

    for i, c in enumerate(fire_count):
        assert c == 5, f"module {i} fired {c} times, expected 5"


if __name__ == "__main__":
    test_two_slots_same_period_independent()
    test_pool_full_returns_invalid()
    test_elapsed_invalid_handle_returns_false()
    test_eight_modules_100ms_all_fire()
    print("OK: 4/4 tests passed")
