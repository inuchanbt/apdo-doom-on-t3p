"""Loopback browser controller and live V65/V66 HID capture for an ATORCH T3P."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import struct
import threading
import time

from t3p_pc_keyboard import SIGNATURE, TEST_USB_ID, connect, send, usb_id

ROOT = Path(__file__).resolve().parents[1]
FULL_SIDE, FAST_SIDE, PALETTE_BYTES, CHUNK = 120, 60, 512, 57
PRODUCTS = ('T3P DOOM V65', 'T3P DOOM V66')


def bmp(frame, side):
    palette = []
    for color, in struct.iter_unpack('<H', frame[:PALETTE_BYTES]):
        palette.append(bytes(((color & 31) * 255 // 31, (color >> 5 & 63) * 255 // 63, (color >> 11) * 255 // 31)))
    pixels = frame[PALETTE_BYTES:]
    # The firmware's frame is column-major. BMP stores top-down BGR rows.
    raster = b''.join(palette[pixels[x * side + y]] for y in range(side) for x in range(side))
    return struct.pack('<2sIHHI', b'BM', 54 + len(raster), 0, 0, 54) + struct.pack(
        '<IiiHHIIiiII', 40, side, -side, 1, 24, 0, len(raster), 0, 0, 0, 0) + raster


class Capture:
    def __init__(self, sequence, now, side):
        self.sequence = sequence
        self.started = now
        self.deadline = now + 6
        self.side = side
        self.size = PALETTE_BYTES + side * side
        self.data = bytearray()

    def add(self, packet):
        if len(packet) != 64 or packet[:4] != b'T3PF' or packet[4] != self.sequence:
            return None
        chunk = int.from_bytes(packet[5:7], 'little')
        if chunk != len(self.data) // CHUNK:
            raise ValueError('Frame packet missing or out of order')
        self.data.extend(packet[7:7 + min(CHUNK, self.size - len(self.data))])
        return bmp(self.data, self.side) if len(self.data) == self.size else None


class Controller:
    def __init__(self):
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.token = secrets.token_urlsafe(24)
        self.connected = False
        self.generation = 0
        self.error = 'Waiting for T3P DOOM V65/V66'
        self.product = ''
        self.preview = True
        self.side = FAST_SIDE
        self.frame = None
        self.frame_id = 0
        self.frame_interval = self.frame_time = self.capture_ms = None
        self.keys = self.pending = 0
        self.last_input = 0

    def set_keys(self, keys):
        with self.lock:
            if keys is None:
                self.keys = self.pending = 0
            elif self.connected:
                self.pending |= keys & ~self.keys
                self.keys = keys
                self.last_input = time.monotonic()

    def link(self, product='', error=''):
        with self.lock:
            self.product = product
            self.connected = bool(product)
            self.generation += int(self.connected)
            self.error = error
            self.keys = self.pending = 0
            self.clear_frame()

    def clear_frame(self):
        self.frame = None
        self.frame_interval = self.frame_time = self.capture_ms = None

    def run(self, hid, identity):
        while not self.stop.is_set():
            device = None
            try:
                device = connect(hid, products=PRODUCTS, identity=identity)
                product = device.get_product_string()
                self.link(product)
                self.exchange(device, product == PRODUCTS[1])
            except (OSError, RuntimeError) as error:
                self.link(error=str(error))
            finally:
                if device is not None:
                    try:
                        send(device, 0, 0, 2)
                    except (OSError, RuntimeError):
                        pass
                    device.close()
            self.stop.wait(1)

    def exchange(self, device, fast_supported):
        sequence = 1
        transfer = None
        next_send = next_frame = 0
        last_reply = time.monotonic()
        while not self.stop.is_set():
            now = time.monotonic()
            if now >= next_send:
                with self.lock:
                    enabled = self.preview
                    side = self.side if fast_supported else FULL_SIDE
                    if now - self.last_input >= .4:
                        self.keys = self.pending = 0
                    keys = self.keys | self.pending
                    self.pending = 0
                flags = 0
                if not enabled:
                    flags = 2
                    transfer = None
                elif (transfer is None or now >= transfer.deadline or transfer.side != side) and now >= next_frame:
                    flags = 3 if side == FAST_SIDE else 1
                    transfer = Capture(sequence, now, side)
                send(device, sequence, keys, flags)
                sequence = (sequence + 1) & 0xff
                next_send = now + .04
            data = bytes(device.read(64, 8))
            if len(data) == 64 and data[:4] in (SIGNATURE, b'T3PF'):
                last_reply = time.monotonic()
            if transfer is not None:
                try:
                    image = transfer.add(data)
                except ValueError:
                    transfer = None
                    next_frame = now + .25
                else:
                    if image is not None:
                        received = time.monotonic()
                        with self.lock:
                            side = self.side if fast_supported else FULL_SIDE
                            if self.preview and transfer.side == side:
                                if self.frame_time is not None:
                                    interval = received - self.frame_time
                                    self.frame_interval = (.75 * self.frame_interval + .25 * interval
                                                           if self.frame_interval else interval)
                                self.frame_time = received
                                self.capture_ms = round((received - transfer.started) * 1000)
                                self.frame = image
                                self.frame_id += 1
                        transfer = None
            if now - last_reply > 3:
                raise TimeoutError('HID replies stopped')


def handler_for(controller, shell):
    web = ROOT / 'web/t3p'
    assets = {
        '/': (web / 'index.html', 'text/html; charset=utf-8'),
        '/controller.css': (web / 'controller.css', 'text/css; charset=utf-8'),
        '/controller.js': (web / 'controller.js', 'text/javascript; charset=utf-8'),
    }
    if shell is not None:
        assets['/t3p.png'] = (shell, 'image/png')

    class Handler(BaseHTTPRequestHandler):
        def send_bytes(self, data, content_type):
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(data)

        def send_json(self, value):
            self.send_bytes(json.dumps(value).encode(), 'application/json')

        def local(self):
            return self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}'

        def do_GET(self):
            if not self.local():
                return self.send_error(403)
            path = self.path.split('?', 1)[0]
            if path == '/api/session':
                return self.send_json({'token': controller.token, 'shell': shell is not None})
            if path == '/api/status':
                with controller.lock:
                    state = dict(connected=controller.connected, generation=controller.generation, error=controller.error,
                                 product=controller.product, fps=round(1 / controller.frame_interval, 1)
                                 if controller.frame_interval else None, capture_ms=controller.capture_ms,
                                 frame=controller.frame_id if controller.frame is not None else None)
                return self.send_json(state)
            if path == '/api/frame.bmp':
                with controller.lock:
                    frame = controller.frame
                return self.send_bytes(frame, 'image/bmp') if frame else self.send_error(404)
            if path not in assets:
                return self.send_error(404)
            file, content_type = assets[path]
            return self.send_bytes(file.read_bytes(), content_type)

        def do_POST(self):
            if (not self.local() or self.headers.get('Origin') != f'http://{self.headers.get("Host")}' or
                    self.headers.get('X-APDO-Token') != controller.token):
                return self.send_error(403)
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 128:
                    raise ValueError
                value = json.loads(self.rfile.read(length))
                if self.path == '/api/keys' and (value is None or type(value) is int and 0 <= value <= 255):
                    controller.set_keys(value)
                elif self.path == '/api/preview' and type(value) is bool:
                    with controller.lock:
                        controller.preview = value
                        if not value:
                            controller.clear_frame()
                elif self.path == '/api/size' and type(value) is int and value in (FAST_SIDE, FULL_SIDE):
                    with controller.lock:
                        if controller.side != value:
                            controller.side = value
                            controller.clear_frame()
                else:
                    raise ValueError
            except ValueError:
                return self.send_error(400)
            self.send_json({'ok': True})

        def log_message(self, *_):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=50595)
    parser.add_argument('--shell', type=Path, help='optional square T3P enclosure photo')
    parser.add_argument('--usb-id', type=usb_id, default=TEST_USB_ID,
                        help='firmware USB VID:PID in hex (default: private test ID 1209:0001)')
    args = parser.parse_args()
    shell = args.shell or ROOT / 'artifacts/pc-preview/t3p.png'
    if not shell.is_file():
        if args.shell:
            parser.error('--shell must point to a PNG file')
        shell = None
    import hid
    controller = Controller()
    with ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(controller, shell)) as server:
        thread = threading.Thread(target=controller.run, args=(hid, args.usb_id), daemon=True)
        thread.start()
        print(f'Open http://127.0.0.1:{server.server_port}/  (Ctrl+C to stop)', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            controller.stop.set()
            thread.join(timeout=1)


if __name__ == '__main__':
    main()
