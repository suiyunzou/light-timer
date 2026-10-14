"""Compatibility entry point for the current local-song scenario checks."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('scene_modes.py')), run_name='__main__')
