# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""E2SAR error types."""


class E2SARError(Exception):
    """Raised when an e2sar_py operation fails.

    Wraps error messages from the underlying C++ result<T> objects.
    """
    pass
