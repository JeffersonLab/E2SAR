#!/usr/bin/env python3
"""Parse Valgrind XML reports and print definite leaks."""
import sys
import glob
import xml.etree.ElementTree as ET

report_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/valgrind-reports"

for xml_file in sorted(glob.glob(f"{report_dir}/*.xml")):
    print(xml_file)
    try:
        tree = ET.parse(xml_file)
    except ET.ParseError:
        continue
    root = tree.getroot()
    leaks = [e for e in root.findall(".//error")
             if e.findtext("kind", "") in ("Leak_DefinitelyLost", "Leak_IndirectlyLost")]
    if not leaks:
        continue
    print(f"\n{'='*60}")
    print(f"File: {xml_file}")
    for e in leaks:
        kind = e.findtext("kind")
        what = e.findtext("xwhat/text") or e.findtext("what", "")
        frames = [f.findtext("fn", "?") for f in e.findall(".//frame")][:5]
        print(f"  [{kind}] {what}")
        print(f"    Stack: {' -> '.join(frames)}")

