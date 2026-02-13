# Overview

Be sure to set PYTHONPATH and E2SARCONFIGDIR variables like so, for example:
```
$ export PYTHONPATH=/home/ubuntu/E2SAR/build/src/pybind
$ export E2SARCONFIGDIR=/home/ubuntu/E2SAR/test/py_test
```
The former points to .so with Python bindings, the latter to a directory with .INI files for segmenter_config.ini and reassembler_config.ini

## Configuration

The test harness relies on segmenter and reassembler INI files located in this directory - specifically the segmenter config sets MTU to 9k and sending rate to 10Gbps to make sure the test passes. 

## Testing

Test with modules: pytest -m <suite> where the suite can be: 
 - 'unit': basic unit tests. Some may fail because they may depend on options that weren't compiled in like NUMA support
 - 'b2b': back-to-back test for segmenter and reassembler code using loopback, do not require a running EJFAT control plane or LB
 - 'cp': control plane tests - require a running control plane and setting EJFAT_URI environment variable appropriately
 - 'lb-b2b': back-to-back tests wiht a real load balancer. Require EJFAT_URI environment variable to be set appropriately

