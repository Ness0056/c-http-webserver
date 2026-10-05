#!/usr/bin/env bash

venv_path="$(pwd)/.venv"
lock_path="$(pwd)/.rnvs_lock"

if [ ! -f "$lock_path" ]; then
	echo "creating virtualenv at $venv_path..."
	if ! python3 -m venv "$venv_path" &> /dev/null; then
		echo "venv module is not available. installing requirements with pip..."
		python3 -m pip install --ignore-installed -r requirements.txt
	else
		source "$venv_path/bin/activate"
		echo "installing requirements..."
		python3 -m pip install -r requirements.txt
	fi
	touch "$lock_path"
fi

source "$venv_path/bin/activate" &> /dev/null
python3 -m pytest "$@"
