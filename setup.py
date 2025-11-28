"""
Setup script for sqlite-vec
Builds the C extension and bundles Python helper functions
"""
import os
import subprocess
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py

# Create sqlite_vec package directory with placeholder __init__.py
# This will be overwritten during build with the real content
os.makedirs('sqlite_vec', exist_ok=True)
if not os.path.exists('sqlite_vec/__init__.py'):
    with open('sqlite_vec/__init__.py', 'w') as f:
        f.write('# Placeholder - will be generated during build\n')

class MakeBuild(build_ext):
    """Custom build that uses Make to compile the extension"""

    def run(self):
        import shutil

        # Run vendor script to get dependencies
        subprocess.check_call(['./scripts/vendor.sh'], shell=True)

        # Build loadable extension
        subprocess.check_call(['make', 'loadable'])

        # Determine extension suffix
        ext_suffix = '.so'
        if os.name == 'nt':
            ext_suffix = '.dll'
        elif os.uname().sysname == 'Darwin':
            ext_suffix = '.dylib'

        # Read VERSION file
        version = open('VERSION').read().strip()

        # Read helper functions
        helper_code = open('bindings/python/extra_init.py').read()

        # Create a Python package that matches upstream API
        with open('sqlite_vec/__init__.py', 'w') as f:
            f.write(f'''
from os import path
import sqlite3

__version__ = "{version}"
__version_info__ = tuple(__version__.split("."))

def loadable_path():
  """ Returns the full path to the sqlite-vec loadable SQLite extension bundled with this package """

  loadable_path = path.join(path.dirname(__file__), "vec0")
  return path.normpath(loadable_path)

def load(conn: sqlite3.Connection)  -> None:
  """ Load the sqlite-vec SQLite extension into the given database connection. """

  conn.load_extension(loadable_path())

''')
            f.write(helper_code)

        # Copy built extension to package directory
        src = os.path.join('dist', f'vec0{ext_suffix}')
        dst = os.path.join('sqlite_vec', f'vec0{ext_suffix}')
        shutil.copy2(src, dst)

    def get_outputs(self):
        # Return the path to the built extension
        ext_suffix = '.so'
        if os.name == 'nt':
            ext_suffix = '.dll'
        elif os.uname().sysname == 'Darwin':
            ext_suffix = '.dylib'
        return [os.path.join('sqlite_vec', f'vec0{ext_suffix}')]

class CustomBuildPy(build_py):
    """Ensure build_ext runs before build_py to generate files"""

    def run(self):
        self.run_command('build_ext')
        super().run()

setup(
    cmdclass={'build_py': CustomBuildPy, 'build_ext': MakeBuild},
    ext_modules=[Extension('vec0', sources=[])],
)
