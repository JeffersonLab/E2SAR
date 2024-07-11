#!/usr/bin/env python3

# this is a script for sniffing and generating various UDP packet formats withing EJ-FAT:
# - sync packets
# - data packets with LB+RE header (pre-load-balancer)
# - data packets with RE header only (post-load-balancer)

import argparse

import time
import sys

from typing import List

from scapy.all import *
from scapy.packet import Packet, bind_layers
from scapy.fields import ShortField, StrLenField

# Sync header
class SyncPacket(Packet):
    name = "SyncPacket"
    fields_desc = [
        StrFixedLenField('preamble', 'LC', 2),
        XByteField('version', 1),
        XByteField('reserved', 0),
        IntField('eventSrcId', 0),
        LongField('eventNumber', 0),
        IntField('avgEventRateHz', 0),
        LongField('unixTimeNano', 0),
    ]
SyncPacketLength = 2 + 1 + 1 + 4 + 8 + 4 + 8

# combined LB header
class LBPacket(Packet):
    name = "LBPacket"
    fields_desc = [
        StrFixedLenField('preamble', 'LB', 2),
        XByteField('version', 2),
        XByteField('nextProto', 1),
        ShortField('rsvd', 0),
        ShortField('entropy', 0),
        LongField('eventNumber', 0),
    ]
LBPacketLength = 2 + 1 + 1 + 2 + 2 + 8

# RE header itself
class REPacket(Packet):
    name = "REPacket"
    fields_desc = [
        BitField('version', 1, 4),
        BitField('rsvd', 0, 4),
        XByteField('rsvd2', 0),
        ShortField('dataId', 0),
        IntField('bufferOffset', 0),
        IntField('bufferLength', 0),
        LongField('eventNumber', 0),
        StrLenField('pld', '', length_from=lambda p: p.bufferLength)
    ] 
REPacketLength = 1 + 1 + 2 + 4 + 4 + 8

# bind to specific UDP destination port
def bind_sync_hdr(dport):
    # Bind the custom layer to the IP protocol
    bind_layers(UDP, SyncPacket, dport=dport)

# bind LB + RE headers to specified port
def bind_lb_hdr(dport):
    bind_layers(UDP, LBPacket, dport=dport)
    bind_layers(LBPacket, REPacket)

# bind RE header to specified port
def bind_re_hdr(dport):
    bind_layers(UDP, REPacket, dport=dport)

# generate a packet with specific field values
def genSyncPkt(ip_addr: str, udp_dport: int, eventSrcId: int, eventNumber: int, avgEventRateHz: int) -> Packet:
    # Create and send a custom packet
    custom_pkt = IP(dst=ip_addr)/UDP(dport=udp_dport)/\
        SyncPacket(eventSrcId=eventSrcId, eventNumber=eventNumber, avgEventRateHz=avgEventRateHz, unixTimeNano=time.time_ns())
    return custom_pkt

# generate packets with LB+RE headers and some payload. break up payload according to mtu
def genLBREPkt(ip_addr: str, udp_port: int, entropy: int, dataId: int, eventNumber: int, payload: str, mtu: int) -> List[Packet]:
    custom_pkts = list()
    max_segment_len = mtu - LBPacketLength - REPacketLength
    if max_segment_len < 0:
        print(f'MTU of {mtu} too short to accommodate LB header {LBPacketLength} and RE header {REPacketLength}')
        sys.exit()
    segment_offset = 0
    segment_end = len(payload) if len(payload) < max_segment_len else max_segment_len
    while segment_offset < len(payload):
        custom_pkt = IP(dst=ip_addr)/UDP(dport=udp_port)/\
            LBPacket(entropy=entropy, eventNumber=eventNumber)/\
            REPacket(dataId=dataId, 
                    bufferOffset=segment_offset, 
                    bufferLength=segment_end - segment_offset,
                    eventNumber=eventNumber, pld=payload[segment_offset:segment_end])
        segment_offset = segment_end
        segment_end += max_segment_len
        segment_end = segment_end if segment_end < len(payload) else len(payload)
        custom_pkts.append(custom_pkt)
    return custom_pkts

# generate a packet with just the RE header and some payload
#
def genREPkt(ip_addr: str, udp_port: int, dataId: int, eventNumber: int, payload:str, mtu: int) -> List[Packet]:
    custom_pkts = list()
    max_segment_len = mtu - REPacketLength
    if max_segment_len < 0:
        print(f'MTU of {mtu} too short to accommodate RE header {REPacketLength}')
        sys.exit()
    segment_offset = 0
    segment_end = len(payload) if len(payload) < max_segment_len else max_segment_len
    while segment_offset < len(payload):
        custom_pkt = IP(dst=ip_addr)/UDP(dport=udp_port)/\
            REPacket(dataId=dataId, bufferOffset=segment_offset, bufferLength=segment_end - segment_offset, 
                    eventNumber=eventNumber, 
                    pld=payload[segment_offset:segment_end])
        segment_offset = segment_end
        segment_end += max_segment_len
        segment_end = segment_end if segment_end < len(payload) else len(payload)
        custom_pkts.append(custom_pkt)
    return custom_pkts

# validate sync packet
def validate_sync_packet(packet):
    if not (packet.preamble == b'LC'):
        return False, f"Preamble must be 'LC' instead of {packet.preamble}"
    if not (packet.version == 1):
        return False, f"Expected version number 1, not {packet.version}"
    return True, ""

# validate LB packet
def validate_lb_packet(packet):
    if not (packet.preamble == b'LB'):
        return False, f"Preamble must be 'LB' instead of {packet.preamble}"
    if not (packet.version == 2):
        return False, f"Expected version number 2, not {packet.version}"
    if not (packet.nextProto == 1):
        return False, f"Expected nextProto 1, not {packet.nextProto}"
    return True, ""
    
# validate RE packet
def validate_re_packet(packet):
    if not (packet.version == 1):
        return False, "Expected version 1, not {packet.version}"
    return True, ""

# generic callback for all headers
def packet_callback(packet):
    if SyncPacket in packet:
        valid, error_msg = validate_sync_packet(packet[SyncPacket])
        if valid:
            packet[IP].show()
        else:
            print(f"Sync validation error: {error_msg}")
    elif LBPacket in packet:
        valid, error_msg = validate_lb_packet(packet[LBPacket])
        if valid:
            packet[IP].show()
        else:
            print(f"LB packet validation error: {error_msg}")
        valid, error_msg = validate_re_packet(packet[REPacket])
        if valid:
            packet[IP].show()
        else:
            print(f"RE packet validation error: {error_msg}")       
    elif REPacket in packet:
        valid, error_msg = validate_re_packet(packet[REPacket])
        if valid:
            packet[IP].show()
        else:
            print(f"RE packet validation error: {error_msg}") 
    else:
        print(f"Unrecognized packet format")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    operations = parser.add_mutually_exclusive_group(required=True)
    operations.add_argument("-l", "--listen", action="store_true", help="listen for incoming packets and try to parse and validate them")
    operations.add_argument("-g", "--generate", action="store_true", help="generate new packets of specific types")
    parser.add_argument("-p", "--port", action="store", help="UDP port (for -l and -g)", default=18347, type=int)
    parser.add_argument("-c", "--count", action="store", help="number of packet streams (if pld larger than mtu, otherwise packets) to generate or expect", default=10, type=int)
    parser.add_argument("--ip", action="store", help="IP address to which to send the packet(s) or listen from")
    parser.add_argument("--show", action="store_true", default=False, help="only show the packet without sending it (with -g)")
    parser.add_argument("--entropy", action="store", default=0, help="entropy value for LB+RE packet", type=int)
    parser.add_argument("--event", action="store", default=0, help="event number for sync, LB+RE and RE packet", type=int)
    parser.add_argument("--rate", action="store", default=100, type=int, help="event rate in Hz for Sync packet")
    parser.add_argument("--dataid", action="store", default=1, type=int, help="data id for RE packet")
    parser.add_argument("--srcid", action="store", default=1, type=int, help="source id for Sync packet")
    parser.add_argument("--mtu", action="store", type=int, default=1500, help="set the MTU length, so LB+RE and RE packets can be fragmented.")
    parser.add_argument("--pld", action="store", help="payload for LB+RE or RE packets. May be broken up if MTU size insufficient")
    packet_types = parser.add_mutually_exclusive_group(required=True)
    packet_types.add_argument("--sync", action="store_true", help="listen for or generate sync packets")
    packet_types.add_argument("--lbre", action="store_true", help="listen for or generate packets with LB+RE header")
    packet_types.add_argument("--re", action="store_true", help="listen for or generate packets with just the RE header")

    args = parser.parse_args()

    if args.generate:
        if not args.ip:
            print(f'--ip option is required (use dotted notation to specify IPv4 address)')
            sys.exit()
        # generate and show a packet
        packets = list()
        for i in range(args.count):
            if args.sync:
                # sync packets
                bind_sync_hdr(args.port)
                packets.append(genSyncPkt(args.ip, args.port, args.srcid, args.event + i, args.rate))
            elif args.lbre:
                # lb+re packets
                bind_lb_hdr(args.port)
                packets.extend(genLBREPkt(args.ip, args.port, args.entropy, args.dataid, args.event, args.pld, args.mtu))
            elif args.re:
                # re packets
                bind_re_hdr(args.port)
                packets.extend(genREPkt(args.ip, args.port, args.dataid, args.event, args.pld, args.mtu))

        if args.show:
            for p in packets:
                p.show()
        else:
            for p in packets:
                p.show()
                send(p)
    elif args.listen:
        # craft a filter
        if args.ip:
            filter = f'ip dst host {args.ip} and udp dst port {args.port}'
        else:
            filter=f'udp dst port {args.port}'

        print(f'Looking for {args.count} packets using filter "{filter}"')

        if args.sync:
            # sync packets
            bind_sync_hdr(args.port)
        elif args.lbre:
            # lb+re packets
            bind_lb_hdr(args.port)
        elif args.re:
            bind_re_hdr(args.port)
        # Start sniffing for packets
        sniff(filter=filter, prn=packet_callback, count=args.count)
