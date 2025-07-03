from os import path
import sqlite3

def loadable_path():
  """ Returns the full path to the sqlite-vec loadable SQLite extension bundled with this package """

  # loadable_path = path.join(path.dirname(__file__), "vec0")
  vec0_path = "@libpath@"+"vec0" # This is replaced by the build system with the actual path to the vec0 library
  return path.normpath(vec0_path)

def load(conn: sqlite3.Connection)  -> None:
  """ Load the sqlite-vec SQLite extension into the given database connection. """

  conn.load_extension(loadable_path())

from .extra_init import *
