# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""CLI sender: sends messages via e2sar Segmenter.

Usage:
    python -m e2sar.cli.send [OPTIONS]

Run in one terminal while running ``python -m e2sar.cli.recv`` in another.
"""

import argparse
import time

import e2sar


def parse_args():
    p = argparse.ArgumentParser(description="e2sar CLI sender")
    p.add_argument(
        "--uri",
        default="ejfat://useless@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:10000",
        help="EJFAT URI for segmenter",
    )
    p.add_argument("--data-id", type=lambda x: int(x, 0), default=0x0505)
    p.add_argument("--event-src-id", type=lambda x: int(x, 0), default=0x11223344)
    p.add_argument("-n", "--count", type=int, default=100, help="Number of messages to send")
    p.add_argument("-s", "--size", type=int, default=1024, help="Message size in bytes")
    p.add_argument("--delay", type=float, default=0.0, help="Delay between sends (seconds)")
    return p.parse_args()


def main():
    args = parse_args()

    ctx = e2sar.Context()
    push = ctx.push(args.uri, data_id=args.data_id, event_src_id=args.event_src_id)

    payload = b"\xAB" * args.size
    t_start = time.monotonic()

    for i in range(args.count):
        push.send(payload)
        if args.delay > 0:
            time.sleep(args.delay)

    elapsed = time.monotonic() - t_start
    print(
        f"Sent {args.count} messages ({args.size} bytes each) in {elapsed:.2f}s "
        f"({args.count / elapsed:.1f} msg/s, "
        f"{args.count * args.size / elapsed / 1e6:.2f} MB/s). "
        f"Frames: {push.send_count}"
    )
    push.close()


if __name__ == "__main__":
    main()
