py = import('python').find_installation('python3', required: true)

py.install_sources(
    'e2sar/__init__.py',
    'e2sar/errors.py',
    'e2sar/context.py',
    'e2sar/get_ip.py',
    'e2sar/segmenter.py',
    'e2sar/reassembler.py',
    subdir: 'e2sar',
)

py.install_sources(
    'e2sar/cli/__init__.py',
    'e2sar/cli/send.py',
    'e2sar/cli/recv.py',
    subdir: 'e2sar/cli',
)
