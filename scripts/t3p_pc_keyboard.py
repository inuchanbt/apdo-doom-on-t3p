"""T3P DOOM HID keyboard controls and an enumeration/echo probe."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import queue
import threading
import time

# Non-unique pid.codes identity for private hardware testing only.
TEST_USB_ID = (0x1209, 0x0001)
PUBLIC_USB_ID = (0x16c0, 0x05df)
PUBLIC_MANUFACTURER = 'usb.pd.epr.240w@gmail.com'
USAGE_PAGE, USAGE = 0xFF61, 1
PRODUCTS = ('T3P DOOM V61', 'T3P DOOM V62', 'T3P DOOM V63', 'T3P DOOM V64', 'T3P DOOM V65', 'T3P DOOM V66')
SIGNATURE = b'T3P1'
KEYS = {
    'space': 1, 'Control_L': 1, 'Control_R': 1, 'w': 2, 'Up': 2,
    'a': 4, 'Left': 4, 'd': 8, 'Right': 8, 'e': 16, 'Escape': 32,
    'Return': 64, 's': 128, 'Down': 128,
}


def packet(sequence, mask, flags=0):
    return (SIGNATURE + bytes([sequence]) + mask.to_bytes(2, 'little') + bytes([flags])).ljust(64, b'\0')


def send(device, sequence, mask, flags=0):
    data = packet(sequence, mask, flags)
    if device.write(b'\0' + data) != 65:
        raise OSError('Short HID write')
    return data


def usb_id(value):
    try:
        vid, pid = (int(part, 16) for part in value.split(':'))
        if 0 < vid < 0xffff and 0 <= pid <= 0xffff:
            return vid, pid
    except ValueError:
        pass
    raise argparse.ArgumentTypeError('Expected a hexadecimal VID:PID pair')


def connect(hid, echoes=1, products=PRODUCTS, identity=None, manufacturer=PUBLIC_MANUFACTURER):
    identities = (identity,) if identity else (PUBLIC_USB_ID, TEST_USB_ID)
    candidates = [device for pair in identities for device in hid.enumerate(*pair)]
    # The shared ID requires whole-string manufacturer AND product matching.
    devices = [d for d in candidates
               if (d.get('usage_page'), d.get('usage')) == (USAGE_PAGE, USAGE)
               and d.get('product_string') in products
               and ((d.get('vendor_id'), d.get('product_id')) != PUBLIC_USB_ID or
                    d.get('manufacturer_string') == manufacturer)]
    if len(devices) != 1:
        details = ', '.join(f"{d.get('product_string')!r} usage={d.get('usage_page')}/{d.get('usage')}"
                            for d in candidates) or 'none'
        raise RuntimeError(f'Expected one T3P DOOM HID interface, found {len(devices)}. '
                           f'HID interfaces at the requested USB IDs: {details}')
    dev = hid.device()
    try:
        dev.open_path(devices[0]['path'])
        for sequence in range(echoes):
            expected = send(dev, sequence, 0)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if bytes(dev.read(64, 100)) == expected:
                    break
            else:
                raise TimeoutError(f'T3P DOOM enumerated; echo {sequence + 1}/{echoes} timed out')
        return dev
    except Exception:
        dev.close()
        raise


def keyboard_window(device):
    import tkinter as tk

    window = tk.Tk()
    window.title('T3P DOOM — HID keyboard test')
    window.configure(padx=18, pady=16)
    message = tk.StringVar(value='HID echo OK. Click this window to enable keyboard input.')
    tk.Label(window, textvariable=message, anchor='w').pack(fill='x')
    tk.Label(window, text='W/S move  •  A/D turn  •  Space fire  •  E use  •  Esc menu  •  Enter confirm',
             pady=12).pack()
    pressed = set()
    pending = 0
    lock = threading.Lock()
    stop = threading.Event()
    problems = queue.SimpleQueue()

    def key(event, down):
        nonlocal pending
        name = event.keysym
        if len(name) == 1:
            name = name.lower()
        value = KEYS.get(name)
        if not value:
            return
        with lock:
            if down and name not in pressed:
                pending |= value
                pressed.add(name)
            elif not down:
                pressed.discard(name)
        return 'break'

    def release(_event=None):
        nonlocal pending
        with lock:
            pressed.clear()
            pending = 0

    def worker():
        nonlocal pending
        sequence = 1
        try:
            last_reply = time.monotonic()
            while not stop.is_set():
                with lock:
                    mask, pending = pending, 0
                    for name in pressed:
                        mask |= KEYS[name]
                send(device, sequence, mask)
                sequence = (sequence + 1) & 0xff
                if bytes(device.read(64, 30)).startswith(SIGNATURE):
                    last_reply = time.monotonic()
                if time.monotonic() - last_reply > 2:
                    raise TimeoutError('HID replies stopped')
                stop.wait(0.025)
        except (OSError, RuntimeError) as error:
            problems.put(str(error))
        finally:
            try:
                send(device, sequence, 0)
            except (OSError, RuntimeError):
                pass  # The firmware lease releases the keys if the link is gone.
            device.close()

    def check_link():
        try:
            error = problems.get_nowait()
        except queue.Empty:
            window.after(200, check_link)
        else:
            release()
            message.set(f'Disconnected — {error}')

    window.bind('<KeyPress>', lambda event: key(event, True))
    window.bind('<KeyRelease>', lambda event: key(event, False))
    window.bind('<FocusOut>', release)
    window.focus_set()
    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    window.after(200, check_link)
    try:
        window.mainloop()
    finally:
        release()
        stop.set()
        thread.join(timeout=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', action='store_true', help='check enumeration and echo with zero input')
    parser.add_argument('--usb-id', type=usb_id, help='restrict detection to this USB VID:PID in hex')
    parser.add_argument('--manufacturer', default=PUBLIC_MANUFACTURER,
                        help='exact manufacturer string for shared HID 16c0:05df')
    args = parser.parse_args()
    try:
        import hid
        echoes = 3 if args.probe else 1
        device = connect(hid, echoes, identity=args.usb_id, manufacturer=args.manufacturer)
        print(f'T3P DOOM HID found; echo verified ({echoes}/{echoes}).', flush=True)
        if args.probe:
            device.close()
        else:
            keyboard_window(device)
    except ImportError:
        parser.exit(1, 'Install hidapi with: py -3 -m pip install hidapi\n')
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
