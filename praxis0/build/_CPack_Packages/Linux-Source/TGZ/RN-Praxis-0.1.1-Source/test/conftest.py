import pytest


def pytest_addoption(parser):
    parser.addoption('--executable', action='store', default='build/hello_world')
    parser.addoption('--ok', action='store_true')
