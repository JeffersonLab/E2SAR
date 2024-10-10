#!/usr/bin/env python3

# this is a script for sniffing, parsing PCAP and generating various UDP packet formats within EJ-FAT:
# - sync packets
# - data packets with LB+RE header (pre-load-balancer)
# - data packets with RE header only (post-load-balancer)

import argparse

import time
import sys

from typing import List

from datetime import datetime

from scapy.all import *
from scapy.packet import Packet, bind_layers
from scapy.fields import ShortField, StrLenField


# Sync header
class SyncPacket(Packet):
    name = "SyncPacket"
    fields_desc = [
        StrFixedLenField('preamble', 'LC', 2),
        XByteField('version', 2),
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

class TruncatedStrLenField(StrLenField):
    __slots__ = ["trunc"]

    def __init__(self, name, default, length_from, truncate_to=10):
        # Add a truncate_to parameter to control the number of characters to show
        super().__init__(name, default, length_from=length_from)
        self.trunc = truncate_to

    def i2repr(self, pkt, x):
        if x is None:
            return ""
        # Truncate the string to `self.truncate_to` characters for display
        return repr(x[:self.trunc].decode("utf-8") + ("..." if len(x) > self.trunc else ""))

# RE header itself
class REPacket(Packet):
    name = "REPacket"
    fields_desc = [
        BitField('version', 1, 4),
        BitField('rsvd', 0, 4),
        XByteField('rsvd2', 0),
        ShortField('dataId', 0),
        IntField('bufferOffset', 0),
        IntField('bufferLength', 0), # note this is Event Length
        LongField('eventNumber', 0),
        TruncatedStrLenField('pld', '', length_from=lambda p: p.bufferLength, truncate_to=20)
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

# bind RE header to specified ports
def bind_re_hdr(dport):
    if (isinstance(dport, list)):
        for p in dport:
            bind_layers(UDP, REPacket, dport=p)
    else:
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
        sys.exit(-1)
    segment_offset = 0
    segment_end = len(payload) if len(payload) < max_segment_len else max_segment_len
    while segment_offset < len(payload):
        custom_pkt = IP(dst=ip_addr)/UDP(dport=udp_port)/\
            LBPacket(entropy=entropy, eventNumber=eventNumber)/\
            REPacket(dataId=dataId, 
                    bufferOffset=segment_offset, 
                    bufferLength=len(payload),
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
        sys.exit(-1)
    segment_offset = 0
    segment_end = len(payload) if len(payload) < max_segment_len else max_segment_len
    while segment_offset < len(payload):
        custom_pkt = IP(dst=ip_addr)/UDP(dport=udp_port)/\
            REPacket(dataId=dataId, bufferOffset=segment_offset, 
                    bufferLength=len(payload), 
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
    if not (packet.version == 2):
        return False, f"Expected version number 2, not {packet.version}"
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
    if Raw in packet:
        del packet[Raw]

    if SyncPacket in packet:
        valid, error_msg = validate_sync_packet(packet[SyncPacket])
        if valid:
            packet[IP].show()
        else:
            print(f"Sync validation error: {error_msg}")
    elif LBPacket in packet:
        valid, error_msg = validate_lb_packet(packet[LBPacket])
        if valid:
            valid, error_msg = validate_re_packet(packet[REPacket])
            if valid:
                packet[IP].show()
            else:
                print(f"RE packet validation error: {error_msg}")
        else:
            print(f"LB packet validation error: {error_msg}")   
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
    operations.add_argument("-a", "--parse", action="store_true", help="parse a pcap file. The recommended way to capture is something like this 'sudo tcpdump -s 200 -tttt -i enp7s0 udp and \\( dst port 19522 or dst port 19523 \\) -w e2sar.pcap'")
    parser.add_argument("-p", "--port", action="store", default=19522, type=int, help="UDP data port (only port for --lbre, starting port for --re)")
    parser.add_argument("-y", "--syncport", action="store", help="UDP sync port", default=19010, type=int)
    parser.add_argument("-n", "--nports", action="store", type=int, default=1, help="number of ports starting with -p to listen on for --re")
    parser.add_argument("-c", "--count", action="store", default=10, type=int, help="number of events (if pld larger than mtu, otherwise packets) to generate or expect or parse")
    parser.add_argument("--ip", action="store", help="IP address to which to send the packet(s) or listen from")
    parser.add_argument("--show", action="store_true", default=False, help="only show the packet without sending it (with -g)")
    parser.add_argument("--entropy", action="store", default=0, type=int, help="entropy value for LB+RE packet")
    parser.add_argument("--event", action="store", default=0, type=int, help="event number for sync, LB+RE and RE packet")
    parser.add_argument("--rate", action="store", default=100, type=int, help="event rate in Hz for Sync packet")
    parser.add_argument("--dataid", action="store", default=1, type=int, help="data id for RE packet")
    parser.add_argument("--srcid", action="store", default=1, type=int, help="source id for Sync packet")
    parser.add_argument("--mtu", action="store", type=int, default=1500, help="set the MTU length, so LB+RE and RE packets can be fragmented.")
    parser.add_argument("--pld", action="store", help="payload for LB+RE or RE packets. May be broken up if MTU size insufficient", default="This is a default payload.")
    parser.add_argument("--iface", action="store", default="all", help="which interface should we listen on (defaults to all)")
    parser.add_argument("-f", "--file", action="store", help="pcap file name to parse", default="./e2sar.pcap")
    packet_types = parser.add_mutually_exclusive_group(required=True)
    packet_types.add_argument("--sync", action="store_true", help="listen for, parse or generate Sync packets")
    packet_types.add_argument("--lbre", action="store_true", help="listen for, parse or generate packets with LB+RE header")
    packet_types.add_argument("--re", action="store_true", help="listen for, parse or generate packets with just the RE header")
    packet_types.add_argument("--lbresync", action="store_true", help="listen for or parse LB+RE and Sync packets")

    args = parser.parse_args()

    # equivalent to -n - not to resolve port numbers to service names
    conf.noenum.add(UDP.sport)
    conf.noenum.add(UDP.dport)

    if args.generate:
        if not args.ip:
            print(f'--ip option is required (use dotted notation to specify IPv4 address)')
            sys.exit(-1)
        # generate and show a packet
        if args.lbresync:
            print('Invalid option combination -g/--generate and --lbresync - multiple layers are bound, pick one')
            sys.exit(-1)

        if args.sync:
            # sync packets
            bind_sync_hdr(args.syncport)
            print(f'Generating Sync(port {args.syncport}) packets for mtu {args.mtu}')
        elif args.lbre:
            # lb+re packets
            bind_lb_hdr(args.port)
            print(f'Generating LBRE(port {args.port}) packets for mtu {args.mtu}')
        elif args.re:
            # re packets (only one port)
            bind_re_hdr(args.port)
            print(f'Generating RE(port {args.port}) packets for mtu {args.mtu}')

        packets = list()
        for i in range(args.count):
            if args.sync:
                # sync packets
                packets.append(genSyncPkt(args.ip, args.port, args.srcid, args.event + i, args.rate))
            elif args.lbre:
                # lb+re packets
                packets.extend(genLBREPkt(args.ip, args.port, args.entropy, args.dataid, args.event, args.pld, args.mtu))
            elif args.re:
                # re packets (only one port)
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
        listeningPorts = list()
        if args.lbre or args.re or args.lbresync:
            listeningPorts = [x + args.port for x in range(0, args.nports)]
        if args.sync or args.lbresync:
            listeningPorts.append(args.syncport)
        portFilter = "or".join([f" dst port {port} " for port in listeningPorts])
        if args.ip:
            filter = f'udp and dst host {args.ip} and \\( {portFilter} \\)'
        else:
            filter=f'udp {portFilter}'

        if args.sync:
            # sync packets
            bind_sync_hdr(args.syncport)
            packet_type = f'Sync(port {args.syncport})'
        elif args.lbre:
            # lb+re packets
            bind_lb_hdr(args.port)
            packet_type = f'LB+RE(port {args.port})'
        elif args.re:
            # multiple ports possible
            portList = [x + args.port for x in range(0, args.nports)]
            bind_re_hdr(portList)
            packet_type = f'RE(ports {portList})'
        elif args.lbresync:
             # sync packets
            bind_sync_hdr(args.syncport)
            # lb+re packets on a single port
            bind_lb_hdr(args.port)
            packet_type = f'LB+RE(port {args.port}) + Sync(port {args.syncport})'   

        # Linux supports "any" shortcut interface specification, but other OSs may not, so we just list
        # all the interfaces first
        if args.iface == 'all':
            interfaces = get_if_list()
        else:
            interfaces = args.iface
        print(f'Looking for {args.count} {packet_type} packets using filter "{filter}" on interfaces {interfaces}')

        # Start sniffing for packets
        sniff(iface=interfaces, filter=filter, prn=packet_callback, count=args.count)
    
    elif args.parse:

        if args.sync:
            # sync packets
            bind_sync_hdr(args.syncport)
            packet_type = f'Sync(port {args.syncport})'
        elif args.lbre:
            # lb+re packets
            bind_lb_hdr(args.port)
            packet_type = f'LB+RE(port {args.port})'
        elif args.re:
            # multiple ports possible
            portList = [x + args.port for x in range(0, args.nports)]
            bind_re_hdr(portList)
            packet_type = f'RE(ports {portList})'
        elif args.lbresync:
             # sync packets
            bind_sync_hdr(args.syncport)
            # lb+re packets on a single port
            bind_lb_hdr(args.port)
            packet_type = f'LB+RE(port {args.port}) + Sync(port {args.syncport})'    

        print(f'Looking for {packet_type} packets in PCAP file {args.file}')
        try:
            if (args.count != 0):
                packets = rdpcap(args.file, count=args.count)
            else:
                packets = rdpcap(args.file)

            for packet in packets:
                packet_time = packet.time
                if packet_time is not None:
                    human_readable_time = datetime.fromtimestamp(float(packet.time))
                    print(f"Timestamp: {human_readable_time}")
                packet_callback(packet)
        except Exception as e:
            print(f'Unable to parse file {args.file} due to exception: {e}')

    print('Finished')
