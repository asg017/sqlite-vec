defmodule Repo do
  use Ecto.Repo,
    otp_app: :my_app,
    adapter: Ecto.Adapters.SQLite3
end

Repo.start_link(
  database: Path.join(__DIR__, "sqlite_vec_test#{System.get_env("MIX_TEST_PARTITION")}.db"),
  pool: Ecto.Adapters.SQL.Sandbox,
  pool_size: 5,
  load_extensions: [SqliteVec.path()]
)

ExUnit.configure(exclude: :slow)
ExUnit.start()
