'''
Test whether the self-built module is working
'''

import sys
import os

# Manualy add the relative path "../build" into sys.path as ninja will create a "packetblast.*.so" there.
# TODO: This needs to be improved by assigning a path to meson install
build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '../build'))
sys.path.append(build_dir)
# print(f"sys.path:\n\t{sys.path}")

import packetblast
print(f"module path: {packetblast.__file__}")
# Use bash command `ifconfig` on Mac or `ip` to get a network device name, e.g., "eth0"/"lo"
print(packetblast.getMTU('eth0', True))
