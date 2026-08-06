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

def main(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    buf = b""
    started = time.time()
    pushed = False
    dropped = [False]
    while time.time() - started < 20:
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
            if op == 0x01:  # GET_STATE -> STATE
                os.write(fd, encode(0x02, corr, b"\x01" + b"\x00" * 11 + b"v0.0.0-stub"))
            else:
                os.write(fd, encode(0xF0, corr, bytes([8])))  # ACK, credits
            if not pushed:
                time.sleep(0.05)
                os.write(fd, encode(0x03, 0x0000, b"stub log line"))
                pushed = True
        time.sleep(0.005)

if __name__ == "__main__":
    main(sys.argv[1])
