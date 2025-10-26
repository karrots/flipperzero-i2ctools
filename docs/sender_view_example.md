# Sender View Example

The sender view presents the essential fields for transmitting a single byte and
immediately reading back a fixed two-byte response. The "Read" length selector
has been removed, so the layout is more compact and focuses on the address,
value, and results.

```
┌──────────────────────────────┐
│ Addr: 0x1C  ◀      ▶         │
│ Value: 0xFF ▲      ▼         │
│ Result: 0x00 0x10            │
│             ┌────────────┐   │
│             │  Send      │   │
└──────────────────────────────┘
```

* **Addr** – Cycle through discovered device addresses with the Left/Right
  buttons.
* **Value** – Adjust the byte to transmit with the Up/Down buttons.
* **Result** – Displays the two-byte response captured after the write.
* **Send** – Press the OK button to send the current value.

The application now always reads back two bytes after a write operation. This
keeps the workflow simple while still showing immediate feedback from the
peripheral.
