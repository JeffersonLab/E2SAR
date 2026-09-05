# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""CLI receiver: receives messages via e2sar Reassembler.

Usage:
    python -m e2sar.cli.recv [OPTIONS]

Run in one terminal while running ``python -m e2sar.cli.send`` in another.
"""

import argparse
import time

import e2sar


def parse_args():
    p = argparse.ArgumentParser(description="e2sar CLI receiver")
    p.add_argument(
        "--uri",
        default="ejfat://useless@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data=127.0.0.1",
        help="EJFAT URI for reassembler",
    )
    p.add_argument("--data-ip", default="127.0.0.1", help="IP to listen on")
    p.add_argument("--data-port", type=int, default=10000, help="UDP port to listen on")
    p.add_argument("-n", "--count", type=int, default=100, help="Number of messages to receive")
    p.add_argument(
        "-t", "--timeout", type=float, default=30.0,
        help="Exit if no message received for this many seconds",
    )
    p.add_argument("--delay", type=float, default=0.0, help="Delay between recvs (seconds)")
    return p.parse_args()


def main():
    args = parse_args()

    ctx = e2sar.Context()
    pull = ctx.pull(args.uri, data_ip=args.data_ip, data_port=args.data_port)

    recv_count = 0
    total_bytes = 0
    t_start = time.monotonic()
    last_recv = t_start

    while recv_count < args.count:
        msg = pull.recv(timeout_ms=100)
        now = time.monotonic()

        if msg is not None:
            recv_count += 1
            total_bytes += len(msg)
            last_recv = now
        elif now - last_recv > args.timeout:
            print(f"Timeout: no message for {args.timeout}s, stopping.")
            break

        if args.delay > 0:
            time.sleep(args.delay)

    elapsed = time.monotonic() - t_start
    print(
        f"Received {recv_count} messages ({total_bytes} bytes total) in {elapsed:.2f}s "
        f"({recv_count / elapsed:.1f} msg/s, "
        f"{total_bytes / elapsed / 1e6:.2f} MB/s). "
        f"Stats: recv_count={pull.recv_count}, "
        f"enqueue_loss={pull.enqueue_loss}, "
        f"reassembly_loss={pull.reassembly_loss}"
    )
    pull.close()


if __name__ == "__main__":
    main()
