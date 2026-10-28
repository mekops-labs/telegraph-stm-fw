#!/usr/bin/env python3
"""Answer the broker's frames the way the STM32 does, over a pty."""
import os, sys, time, struct

SOF = 0xAA

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def encode(opcode, corr, payload=b""):
    body = struct.pack("<HBH", len(payload), opcode, corr) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))

def entry(kind, size, name):
    """One record of an FS_LIST reply."""
    return bytes([kind]) + struct.pack("<I", size) + bytes([len(name)]) + name

def usbdev(channel, kind, name):
    """One record of a USB_DEVS reply."""
    return bytes([channel, kind, len(name)]) + name

def state(time_s, frames):
    """The 12 fixed bytes of a STATE reply, then the firmware version."""
    fixed = struct.pack("<IhHHBB", time_s, 215, frames & 0xFFFF, 0, 0, 1)
    return fixed + b"v0.0.0-stub"

def settings_reply(st):
    """The SETTINGS reply: levels, period, offsets, then the outside value."""
    ext = st["ext"]
    return struct.pack("<BBHHhhBh", st["digits"], st["panels"], st["sleep"],
                       st["wake"], 0, 0, 0 if ext is None else 1, ext or 0)

def main(dev):
    # What the board holds. STUB_* seeds it the way a stored record would.
    st = {
        "digits": int(os.environ.get("STUB_DIGITS", "5")),
        "panels": int(os.environ.get("STUB_PANELS", "8")),
        "sleep": int(os.environ.get("STUB_SLEEP", "65535")),
        "wake": int(os.environ.get("STUB_WAKE", "65535")),
        "ext": None,
    }
    lifetime = int(os.environ.get("STUB_SECONDS", "20"))
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    buf = b""
    started = time.time()
    pushed = False
    dropped = [False]
    # The clock of the stub: STUB_TIME is its DS3231, and it keeps running.
    # STUB_RESET_AT is the count of frames after which it resets and counts
    # from zero again.
    delta = int(os.environ.get("STUB_TIME", "0")) - time.time()
    reset_at = int(os.environ.get("STUB_RESET_AT", "0"))
    frames = 0
    while time.time() - started < lifetime:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf += chunk
        while buf:
            if buf[0] == SOF and len(buf) < 8:
                break
            if buf[0] != SOF:
                # raw bytes: echo them, the way a bootloader answers
                os.write(fd, buf[:1])
                print(f"stub: echoed raw byte {buf[0]:#04x}", flush=True)
                buf = buf[1:]
                continue
            ln = struct.unpack("<H", buf[1:3])[0]
            total = ln + 8
            if len(buf) < total:
                break
            frame, buf = buf[:total], buf[total:]
            op, corr = frame[3], struct.unpack("<H", frame[4:6])[0]
            print(f"stub: got opcode 0x{op:02x} id 0x{corr:04x} len {ln}", flush=True)
            if os.environ.get("DROP_FIRST") and not dropped[0]:
                dropped[0] = True
                print("stub: dropped it", flush=True)
                continue
            if op == 0x01 and reset_at and frames >= reset_at:
                frames = 0
                print("stub: reset", flush=True)
            frames += 1
            if op == 0x01:  # GET_STATE -> STATE
                os.write(fd, encode(0x02, corr, state(int(time.time() + delta), frames)))
            elif op == 0x04:  # SET_TIME -> ACK, with the optional offset
                clock = struct.unpack("<I", frame[6:10])[0]
                delta = clock - time.time()
                offset = struct.unpack("<h", frame[10:12])[0] if ln >= 6 else None
                print(f"stub: set time {clock} offset {offset}", flush=True)
                os.write(fd, encode(0xF0, corr, bytes([8])))
            elif op == 0x20:  # FS_LIST -> one file and one directory
                entries = entry(0x00, 12, b"backup.bin") + entry(0x01, 0, b"logs")
                os.write(fd, encode(0x20, corr, struct.pack("<H", 0xFFFF) + entries))
            elif op == 0x21:  # FS_READ -> the part at the requested offset
                offset = struct.unpack("<I", frame[6:10])[0]
                data = b"BACKUP-BYTES"[offset:]
                os.write(fd, encode(0x21, corr, struct.pack("<I", offset) + data))
            elif op == 0x30:  # USB_LIST -> USB_DEVS
                devs = usbdev(0, 0x00, b"ttyACM0") + usbdev(0xFF, 0x01, b"sda1")
                os.write(fd, encode(0x31, corr, devs))
            elif op == 0x34:  # USB_SUB -> ACK, then the device speaks
                os.write(fd, encode(0xF0, corr, bytes([8])))
                if frame[7]:
                    time.sleep(0.05)
                    os.write(fd, encode(0x33, 0x0000, b"\x00" + b"device says hello"))
            elif op == 0x11:  # SET_BRIGHT -> ACK, one byte sets both
                st["digits"] = frame[6]
                st["panels"] = frame[7] if ln >= 2 else frame[6]
                print(f"stub: set brightness {st['digits']} {st['panels']}", flush=True)
                os.write(fd, encode(0xF0, corr, bytes([8])))
            elif op == 0x12:  # SET_SLEEP -> ACK
                st["sleep"], st["wake"] = struct.unpack("<HH", frame[6:10])
                print(f"stub: set sleep {st['sleep']} {st['wake']}", flush=True)
                os.write(fd, encode(0xF0, corr, bytes([8])))
            elif op == 0x07:  # SET_EXTTEMP -> ACK, an empty payload clears it
                if ln == 0:
                    st["ext"] = None
                    print("stub: clear exttemp", flush=True)
                    os.write(fd, encode(0xF0, corr, bytes([8])))
                elif ln == 2:
                    st["ext"] = struct.unpack("<h", frame[6:8])[0]
                    print(f"stub: set exttemp {st['ext']}", flush=True)
                    os.write(fd, encode(0xF0, corr, bytes([8])))
                else:
                    os.write(fd, encode(0xF1, corr, bytes([2])))
            elif op == 0x08:  # GET_SETTINGS -> SETTINGS
                os.write(fd, encode(0x09, corr, settings_reply(st)))
            elif op == 0x13:  # SET_TEXT -> ACK, the body in hex
                print(f"stub: text hex {frame[8:6 + ln].hex()}", flush=True)
                os.write(fd, encode(0xF0, corr, bytes([8])))
            elif op == 0x19:  # RELEASE -> ACK
                print(f"stub: release panel {frame[6]}", flush=True)
                os.write(fd, encode(0xF0, corr, bytes([8])))
            else:
                os.write(fd, encode(0xF0, corr, bytes([8])))  # ACK, credits
            if not pushed:
                time.sleep(0.05)
                os.write(fd, encode(0x03, 0x0000, b"stub log line"))
                pushed = True
        time.sleep(0.005)

if __name__ == "__main__":
    main(sys.argv[1])
