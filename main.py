#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Shomy
#

import argparse
import logging

from lib.device import Device


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        '-v',
        dest='verbose',
        action='store_true',
        help='Enable verbose output',
    )

    subparsers = parser.add_subparsers(
        dest='command', required=True, help='Command to execute'
    )

    subparsers.add_parser(
        'unlock', help='Unlock bootloader and fastboot capabilities'
    )

    subparsers.add_parser(
        'patch', help='Patch preloader to allow loading an unsigned DA'
    )

    reset_parser = subparsers.add_parser(
        'factory-reset',
        help='Invalidate Android data/cache/metadata filesystems via a tiny preloader payload',
    )
    reset_parser.add_argument(
        '--yes-really-reset',
        action='store_true',
        help='Required confirmation: permanently destroys Android user data',
    )

    args = parser.parse_args()

    logging.basicConfig(
        format='%(levelname)s: %(message)s',
        level=logging.DEBUG if args.verbose else logging.INFO,
    )

    if args.command == 'factory-reset':
        if not args.yes_really_reset:
            parser.error(
                'factory-reset is destructive; re-run with --yes-really-reset '
                'after reviewing the target partition geometry'
            )
        path = 'bin/factory-reset.bin'
    elif args.command == 'unlock':
        path = 'bin/unlock.bin'
    else:
        path = 'bin/patch.bin'

    with open(path, 'rb') as f:
        data = f.read()
        logging.info(f'Using payload from {path}')

    if args.command == 'factory-reset':
        logging.warning('THIS WILL DESTROY ALL ANDROID USER DATA.')
        logging.warning('It preserves FRP and the separate GPT partition named metadata.')
        logging.warning('Payload will write only 16 KiB total across cache/userdata/md_udc.')

    device = Device(None)

    logging.info('Waiting for MediaTek Preloader device (0E8D:2000)...')

    device.find_device()
    device.handshake()
    device.identify()

    device.send_image('lk', data)
    device.boot_image('lk')


if __name__ == '__main__':
    main()
