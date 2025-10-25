# I2C Sender UI Walkthrough

The updated sender screen can render up to 128 bytes from the last reply. A long OK press
cycles the active field so the D-pad always edits whichever area shows the on-screen
arrow hints.

```
┌──────────────────────────────────────────────┐
│Addr: 0x1A                                     │
│Value: 0x08   ↑  ↓                              │
│Read:  32 bytes                                │
│Result: 32B   1-4/6         ↑ ↓                │
│00 01 02 03 04 05 06 07                        │
│08 09 0A 0B 0C 0D 0E 0F                        │
│10 11 12 13 14 15 16 17                        │
│18 19 1A 1B 1C 1D 1E 1F                        │
│ [◀ Addr]  Hold OK: change  Up/Down: scroll    │
└──────────────────────────────────────────────┘
```

* **Value controls**
  * When the `Value` line shows the arrow icons, Up/Down increment or decrement the byte
    written before the read.
  * Long presses still step in ±5 increments.

* **Length controls**
  * After a long OK press moves the focus to `Read`, the arrows appear next to that line.
    Up/Down adjust the requested read length (1–128 bytes) with long-press ±5 steps.

* **Result scrolling**
  * When the result pager shows arrow icons, Up/Down scroll through the rows (long
    presses jump four rows). The `1-4/6` marker reflects the currently visible slice.

Regardless of focus, tapping OK sends the transaction immediately. The Send button at the
bottom highlights this action and the footer reminders (`Hold OK: change`, plus the
dynamic `Up/Down` hint) explain how to switch and interact with the current field while
you are learning the workflow.

When the result is shorter than four rows, the pagination indicator disappears and the
response grid collapses to only the populated rows. Errors or empty responses replace the
grid with `I2C error` or `No data` messages so the UI still provides quick feedback.
