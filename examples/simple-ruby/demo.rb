require 'sqlite3'
require 'sqlite_vec'


db = SQLite3::Database.new(':memory:')
db.enable_load_extension(true)
SqliteVec.load(db)
db.enable_load_extension(false)

sqlite_version, vec_version = db.execute("select sqlite_version(), vec_version()").first
puts "sqlite_version=#{sqlite_version}, vec_version=#{vec_version}"

db.execute("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")

items = [
  [1, [0.1, 0.1, 0.1, 0.1]],
  [2, [0.2, 0.2, 0.2, 0.2]],
  [3, [0.3, 0.3, 0.3, 0.3]],
  [4, [0.4, 0.4, 0.4, 0.4]],
  [5, [0.5, 0.5, 0.5, 0.5]],
]

db.transaction do
  items.each do |item|
    db.execute("INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)", [item[0], item[1].pack("f*")])
  end
end

query = [0.3, 0.3, 0.3, 0.3]
rows = db.execute(<<-SQL, [query.pack("f*")])
  SELECT
    rowid,
    distance
  FROM vec_items
  WHERE embedding MATCH ?
  ORDER BY distance
  LIMIT 3
SQL

puts rows
