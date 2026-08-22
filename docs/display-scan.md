# The scan of the panels

The panels hold an image only while something drives them, thus a thread scans
them row by row without stopping. What follows is why that loop is shaped the
way it is; the source states the invariants alone.

## The rate of the rows is the limit, not the work in a row

Both panels take the same row in one pass, and a full image is 16 of them.

**Above about 1250 row events each second the UART loses bytes at 460800
baud.** The cost is not the transfer of a row — that is a few microseconds of
direct register writes — but the events themselves: an interrupt and a change
of thread each hold the other interrupts for a time, and the receive buffer of
the protocol overflows in that window.

So the period of a row is chosen against the link, not against the display.

## The scan runs below the task of the protocol

The opposite order loses frames. With the rows above the protocol task, that
task waits behind every row, its receive buffer fills, and the bytes of a
burst run past the end of it. **Measured: a burst of 200 frames delivered
none.**

The price of this order is a row that arrives late while the protocol holds
the CPU, which shows as a short flicker. That is the cheaper failure — a
flicker repairs itself at the next pass and a lost frame does not.

For the same reason the scan sits below the shell: it is a wait loop with no
point to stop at, so a thread above the shell would stop the shell outright.
The shell waits for console input almost all of the time, thus the scan still
gets the CPU it needs.

## The timer drives the rows, and an interrupt takes no mutex

The scan of one row runs from a timer interrupt. The work is the transfer of
that row, and the wait between rows costs no CPU at all — which is what gives
the tasks of the board the time the earlier busy-wait loop consumed.

The lock of the framebuffer is a mutex, and an interrupt cannot take one.
**A writer of the framebuffer therefore stops the timer around its change**,
which is what keeps a scan pass from ever showing half of one image and half
of another.

## The digits follow the clock, not a count of waits

The thread of the board moves the animations and keeps the digits. It reads
the system clock fresh on every wait rather than counting the waits: a wait
takes at least its period and often more, so a count of them drifts and the
digits miss a second from time to time.

The clock itself is the battery-backed DS3231, thus reading the time uses no
bus. Only the temperature does.

## What bounds a wrapped text

A wrapped text is rendered into the animation source, so its bitmap must fit
`ANIM_SRC_MAIN`. At a 70-pixel panel and the shortest line height the fonts
offer — 7 rows, the compact font — **seven lines plus one gap line is 504 of
the 512 bytes the source holds**, which is where the line bound comes from.

A word wider than the line breaks mid-word, at the last character that fits.
A caller that receives fewer lines than its text needed has lost the
remainder, the same way an over-long source elsewhere is capped rather than
refused.

## Why a long text scrolls by itself

The board draws the text into the source, thus a scrolling message costs one
frame of the protocol and not one frame per step. The source wraps from its
own end back to its own start, and a gap of blank columns after the text keeps
that wrap from reading as the text running into itself.

A window left at 0/0 takes its frame size from the sprite file — the frame
width is the file's step, the height is the file's height — which stops a
window wider than one frame from showing part of the next one beside it.
