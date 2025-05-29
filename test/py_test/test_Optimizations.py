"""
The pytest for cpp class e2sar::Optimizations.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path. E.g,
# import sys

# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind
"""

import pytest

# Make sure the compiled module is added to your path
import e2sar_py
opt = e2sar_py.Optimizations


@pytest.mark.unit
def test_code_enum():
    """Test the Code enum bindings."""
    assert int(opt.Code.none) == 0
    assert int(opt.Code.sendmmsg) == 1
    assert int(opt.Code.liburing_send) == 2
    assert int(opt.Code.liburing_recv) == 3
    assert int(opt.Code.unknown) == 15


@pytest.mark.unit
def test_to_word():
    """Test the toWord function."""
    assert opt.toWord(opt.Code.none) == 1 << int(opt.Code.none)
    assert opt.toWord(opt.Code.sendmmsg) == 1 << int(opt.Code.sendmmsg)
    assert opt.toWord(opt.Code.liburing_send) == 1 << int(opt.Code.liburing_send)
    assert opt.toWord(opt.Code.liburing_recv) == 1 << int(opt.Code.liburing_recv)
    assert opt.toWord(opt.Code.unknown) == 1 << int(opt.Code.unknown)


@pytest.mark.unit
def test_to_string():
    """Test the toString function."""
    assert opt.toString(opt.Code.none) == "none"
    assert opt.toString(opt.Code.sendmmsg) == "sendmmsg"
    assert opt.toString(opt.Code.liburing_send) == "liburing_send"
    assert opt.toString(opt.Code.liburing_recv) == "liburing_recv"
    assert opt.toString(opt.Code.unknown) == "unknown"


@pytest.mark.unit
def test_from_string():
    """Test the fromString function."""
    assert opt.fromString("none") == opt.Code.none
    assert opt.fromString("sendmmsg") == opt.Code.sendmmsg
    assert opt.fromString("liburing_send") == opt.Code.liburing_send
    assert opt.fromString("liburing_recv") == opt.Code.liburing_recv
    assert opt.fromString("random_invalid") == opt.Code.unknown


@pytest.mark.unit
def test_available_as_strings():
    """Test the availableAsStrings function."""
    available = opt.availableAsStrings()
    assert isinstance(available, list)
    assert all(isinstance(item, str) for item in available)


@pytest.mark.unit
def test_available_as_word():
    """Test the availableAsWord function."""
    word = opt.availableAsWord()
    assert isinstance(word, int)


@pytest.mark.unit
def test_select_by_string():
    """Test the select function with string inputs."""
    opt_names = ["sendmmsg"]
    result = opt.select(opt_names)
    assert result.has_error() is False, f"Error: {result.error().message}"
    assert isinstance(result.value(), int)


@pytest.mark.unit
def test_select_by_enum():
    """Test the select function with enum inputs."""
    # According to the underlying cpp implementation, "sendmmg" cannot be
    # used together with "liburing_send"/"liburing_recv".
    opt_enums = [opt.Code.sendmmsg]
    result = opt.select(opt_enums)
    assert result.has_error() is False, f"Error: {result.error().message}"
    assert isinstance(result.value(), int)


@pytest.mark.unit
def test_selected_as_strings():
    """Test the selectedAsStrings function."""
    selected = opt.selectedAsStrings()
    assert isinstance(selected, list)
    assert all(isinstance(item, str) for item in selected)


@pytest.mark.unit
def test_selected_as_word():
    """Test the selectedAsWord function."""
    word = opt.selectedAsWord()
    assert isinstance(word, int)


@pytest.mark.unit
def test_selected_as_list():
    """Test the selectedAsList function."""
    selected = opt.selectedAsList()
    assert isinstance(selected, list)
    assert all(isinstance(item, opt.Code) for item in selected)


@pytest.mark.unit
def test_is_selected():
    """Test the isSelected function."""
    assert isinstance(opt.isSelected(opt.Code.sendmmsg), bool)
    assert isinstance(opt.isSelected(opt.Code.liburing_send), bool)
