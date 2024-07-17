import sqlite3
EXT_PATH = "dist/vec0"
db = sqlite3.connect(":memory:")

db.execute(
    "create temp table base_functions as select name from pragma_function_list"
)
db.execute("create temp table base_modules as select name from pragma_module_list")

db.enable_load_extension(True)
db.load_extension(EXT_PATH)
db.row_factory = sqlite3.Row
loaded_functions = db.execute(
    "select name, narg from pragma_function_list where name not in (select name from base_functions) order by name"
).fetchall()

db.execute(
    "create temp table loaded_modules as select name from pragma_module_list where name not in (select name from base_modules) order by name"
)

db.row_factory = sqlite3.Row

def trace(sql):
  print(sql)
db.set_trace_callback(trace)

def spread_args(n):
    return ",".join(["?"] * n)

for f in loaded_functions:
  v = [None, 1, 1.2, b"", '',  "asdf", b"\xff", b"\x00", "\0\0\0\0"]
  for x in v:
    try:
      db.execute("select {}({}); ".format(f['name'],spread_args(f['narg'])), [x] * f['narg'])
    except sqlite3.OperationalError:
      pass
