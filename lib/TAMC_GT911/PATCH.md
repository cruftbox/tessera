# TAMC_GT911 (vendored, patched)

This is `tamctec/TAMC_GT911` 1.0.2 with one local patch.

## Why it's vendored

The Guition ESP32-S3-4848S040 wires the GT911 with **no INT/RST pins**, so
`PIN_TOUCH_INT` / `PIN_TOUCH_RST` are `-1`. The library stores those in `uint8_t`
fields, so `-1` becomes `255`, and `reset()` then calls `pinMode(255, ...)` →
runtime `Invalid IO 255` errors and a failed touch init.

## The patch

`TAMC_GT911::reset()` guards every INT/RST pin operation:

```cpp
bool hasInt = (pinInt != 255);
bool hasRst = (pinRst != 255);
if (hasInt) { ... }
if (hasRst) { ... }
```

So when a pin is unset (255) the library skips the `pinMode`/`digitalWrite` calls.

Vendored here (instead of `lib_deps`) so the patch survives a fresh
`git clone` + build — a registry copy under `.pio/libdeps/` would be re-fetched
unpatched.
