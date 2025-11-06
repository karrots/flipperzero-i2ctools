# AGENTS.md

This file provides guidance to AI coding agents when working with code in this repository.

## Project Overview

FlipperZero I2C Tools is an external FAP (Flipper Application Package) providing I2C peripheral interaction tools:
- **Scanner**: Discover I2C device addresses on the bus
- **Sniffer**: Capture and analyze I2C traffic in real-time
- **Sender**: Send custom I2C commands and read responses
- **Crypto**: Interface with ATECC608B secure crypto elements via CryptoAuthLib

**Hardware Requirements**: C0 (SCL), C1 (SDA), GND. Target must use 3.3V logic levels. Use ISO1541 isolator if voltage level uncertain.

## Build System

This project uses FlipperZero's Unified Build Tool (uFBT).

### Common Commands

```bash
# Build the FAP
ufbt build

# Format code (applies .clang-format rules)
ufbt format

# Lint code (clang-format is ignored in CI)
ufbt lint --ignore clang-format

# Launch CLI for device interaction
ufbt cli
```

### CI/CD Pipeline

GitHub Actions runs on push/PR:
1. **Lint** using `flipperdevices/flipperzero-ufbt-action@v0.1.3`
2. **Build** FAP with `sdk-channel: release`
3. **Upload** artifacts (FAP file auto-named)

## Code Architecture

### Multi-View Event-Driven Pattern

The app uses a ViewPort-based GUI with message queue input handling:

```
i2ctools_app (entry point in i2ctools.c)
├── ViewPort (canvas for drawing)
├── FuriMessageQueue (input events)
├── FuriMutex (thread-safe state access)
└── Views (each with alloc/free/draw/input)
    ├── main_view (menu hub)
    ├── scanner_view (device discovery)
    ├── sniffer_view (traffic capture)
    ├── sender_view (command execution)
    ├── config_view (bus configuration)
    ├── infos_view (help/about)
    └── crypto_view (ATECC608B interface)
```

### Core Application State

Defined in [i2ctools_i.h](i2ctools_i.h):

```c
typedef struct {
    FuriMutex* mutex;              // Thread-safe access
    ViewPort* view_port;           // GUI drawing surface
    i2cMainView* main_view;        // Menu state
    i2cScanner* scanner;           // Scanner state
    i2cSniffer* sniffer;           // Sniffer state
    i2cSender* sender;             // Sender state
    CryptoView* crypto;            // Crypto UI state
} i2cTools;
```

### Key Source Files

| File | Purpose |
|------|---------|
| [i2ctools.c](i2ctools.c) | Main entry point, event loop, view dispatcher |
| [i2ctools_i.h](i2ctools_i.h) | Internal header with `i2cTools` state struct |
| [i2cscanner.c](i2cscanner.c) | I2C address scanning logic |
| [i2csniffer.c](i2csniffer.c) | I2C traffic capture and analysis |
| [i2csender.c](i2csender.c) | Custom I2C command sending |
| [views/crypto_view.c](views/crypto_view.c) | ATECC608B UI and actions |
| [crypto/crypto_service.c](crypto/crypto_service.c) | CryptoAuthLib session management |
| [crypto/hal_furi_i2c.c](crypto/hal_furi_i2c.c) | Custom HAL bridging CryptoAuthLib to FlipperZero I2C |
| [crypto/atca_config.h](crypto/atca_config.h) | CryptoAuthLib feature flags and timing configuration |

## CryptoAuthLib Integration

### Configuration

ATECC608B support configured in [crypto/atca_config.h](crypto/atca_config.h):
- Device: ATECC608
- Interface: I2C @ 100 kHz
- Default address: 0x60
- Post-command delay: 25ms
- Certificate support enabled (`ATCACERT_COMPCERT_EN=1`)

### Custom HAL Implementation

[crypto/hal_furi_i2c.c](crypto/hal_furi_i2c.c) bridges CryptoAuthLib to FlipperZero's `furi_hal_i2c` driver:
- Bus acquisition/release with proper locking
- 20ms I2C timeout handling
- Wake/sleep command support

### Crypto Actions

Available in [views/crypto_view.c](views/crypto_view.c):
- **Detect Device**: Probe for chip presence
- **Chip Info**: Read serial number, revision, lock states
- **Random**: Generate 32-96 byte random numbers
- **Self-Test**: Run chip self-test
- **Slot Peek**: Safe read-only operations on 16 key slots (0-15)
- **Sleep Device**: Low-power state

### CLI Specification

Planned full CLI support documented in [docs/crypto_cli.md](docs/crypto_cli.md):
- Session persistence for chained commands
- JSON output support
- SD card file I/O
- Commands: config read/write, data operations, hash, MAC/HMAC, nonce, etc.

## Build Configuration

### Application Manifest

[application.fam](application.fam) defines:
- App ID: `i2ctools`
- Entry point: `i2ctools_app()`
- Stack: 2KB
- Category: GPIO
- Version: 1.1
- Author: @NaejEL

### CryptoAuthLib Library

Integrated as `fap_private_libs` with:
- Basic ATECC608B commands (`lib/calib/*.c`)
- Certificate support (`lib/atcacert/*.c`)
- TNG (Trust & Go) support (`app/tng/*.c`)
- Software crypto (SHA1/SHA2)
- Custom compile flags:
  - `ATCA_HAL_CUSTOM` (uses custom HAL)
  - `ATCA_POST_DELAY_MSEC=25` (device settling time)
  - Warnings suppressed: unused-parameter, incompatible-pointer-types, unused-variable

## Coding Patterns and Conventions

### Mutex-Protected State Access

Always acquire mutex before modifying shared state:

```c
if(furi_mutex_acquire(i2ctools->mutex, 200) != FuriStatusOk) {
    return; // Timeout
}
// ... modify shared state ...
furi_mutex_release(i2ctools->mutex);
```

### Message Queue for Input (Non-blocking UI)

```c
FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
while(furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk) {
    // Process input event
}
```

### Dialog Helpers (Blocking Interaction)

```c
DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
DialogMessage* msg = dialog_message_alloc();
dialog_message_set_text(msg, "Message", 64, 32, AlignCenter, AlignCenter);
dialog_message_show(dialogs, msg);
dialog_message_free(msg);
furi_record_close(RECORD_DIALOGS);
```

### Resource Management

- Use `furi_record_open()` / `furi_record_close()` for system services
- Views follow `<component>_alloc()` / `<component>_free()` pattern
- Ensure cleanup in all exit paths (especially crypto view, sniffer interrupts)

### Code Style

Follow [.clang-format](.clang-format) rules (LLVM-based). Format before committing:

```bash
ufbt format
```

## Git Workflow

Always use the Git MCP server for git operations (per user's global CLAUDE.md).

Current development:
- Active branch: `codex/add-cryptoauthlib-and-hal-for-atecc608b`
- Main branch: `main`
- Recent work: CryptoAuthLib integration, HAL implementation, code formatting

## Important Notes

1. **3.3V Logic Only**: FlipperZero GPIO is 3.3V. Use level shifters/isolators for 5V I2C devices.

2. **No Unit Tests**: Manual testing on real hardware required. FlipperZero HAL cannot be easily mocked.

3. **CryptoAuthLib Submodule**: Run `git submodule update --init --recursive` after cloning.

4. **Safe Slots**: Crypto view only performs read-safe operations. Avoid writing/locking slots without understanding ATECC608B behavior (see [docs/RESEARCH_ATECC608B_LOCK_BEHAVIOR.md](docs/RESEARCH_ATECC608B_LOCK_BEHAVIOR.md)).

5. **Stack Size**: Limited to 2KB. Be cautious with large stack allocations and deep recursion.

6. **I2C Bus Conflicts**: Always release I2C bus properly in cleanup paths. Sniffer view must restore bus state when exiting.
