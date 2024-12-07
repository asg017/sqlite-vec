defmodule SqliteVecTest do
  use ExUnit.Case

  @tag :slow
  test "supported downloads should work" do
    assert :ok = OctoFetch.Test.test_all_supported_downloads(SqliteVec.Downloader)
  end
end
