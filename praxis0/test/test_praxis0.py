"""
Tests for RN Praxis 0
"""

import pytest
import subprocess

from util import KillOnExit
from util import is_ubuntu20_eecs


@pytest.fixture
def executable(request):
    return request.config.getoption('executable')

@pytest.fixture
def ok(request):
    return request.config.getoption('ok')


@pytest.mark.timeout(3)
def test_output(executable, ok):
    """Run the program and check the output"""
    try:
        proc = KillOnExit([executable], stdout=subprocess.PIPE, text=True)
        out, err = proc.communicate()
        print(out, err)
    except FileNotFoundError:
        raise FileNotFoundError("Test nicht möglich, die Binary-Datei 'build/hello_world' existiert nicht. Haben Sie in CMakeLists 'add_executable' richtig konfiguriert und sie korrekt erstellt? (Konsole: 'cmake -B build -D CMAKE_BUILD_TYPE=Debug && make -C build')")

    # Check the stdout output (from your printf)
    error_msg = f"Das Programm hat '{out}' ausgegeben, aber 'Hello World!' war erwartet"
    assert out == "Hello World!", error_msg

    assert is_ubuntu20_eecs() or ok, "System is not EECS Ubuntu 20 test system. Please follow the \'Ubuntu 20 EECS Testing Guide\' on ISIS for properly testing submissions."
