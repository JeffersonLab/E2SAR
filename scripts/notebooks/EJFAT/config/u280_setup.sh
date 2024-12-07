#!/bin/bash
sn-cfg --tls-insecure show device info
sn-cfg --tls-insecure configure switch -s host0:host0 -s host1:host1 -s port0:port0 -s port1:port1
sn-cfg --tls-insecure configure switch -e app0:host0:host0 -e app0:host1:host1 -e app0:port0:port0 -e app0:port1:port1
sn-cfg --tls-insecure configure switch -e app1:host0:host0 -e app1:host1:host1 -e app1:port0:port0 -e app1:port1:port1
sn-cfg --tls-insecure configure switch -e bypass:host0:port0 -e bypass:host1:port1 -e bypass:port0:host0 -e bypass:port1:host1
sn-cfg --tls-insecure configure switch -i host0:bypass -i host1:bypass -i port0:app0 -i port1:app0
sn-cfg --tls-insecure show switch config
sn-cfg --tls-insecure configure host --host-id 0 --dma-base-queue 0 --dma-num-queues 1
sn-cfg --tls-insecure configure host --host-id 1 --dma-base-queue 1 --dma-num-queues 1
sn-cfg --tls-insecure show host
sn-cfg --tls-insecure configure port --state enable
sn-cfg --tls-insecure show port status
