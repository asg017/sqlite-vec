# Using `sqlite-vec` in Ruby

![Gem](https://img.shields.io/gem/v/sqlite-vec?color=red&logo=rubygems&logoColor=white)

Ruby developers can use `sqlite-vec` with the [`sqlite-vec` Gem](https://rubygems.org/gems/sqlite-vec).


```bash
gem install sqlite-vec
```

You can then use `SqliteVec.load()` to load `sqlite-vec` SQL functions in a given SQLite connection.

```ruby
require 'sqlite3'
require 'sqlite_vec'

db = SQLite3::Database.new(':memory:')
db.enable_load_extension(true)
SqliteVec.load(db)
db.enable_load_extension(false)

result = db.execute('SELECT vec_version()')
puts result.first.first

```

See
[`simple-ruby/demo.rb`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-ruby/demo.rb)
for a more complete Ruby demo.

## Working with vectors in Ruby

If your embeddings are provided as a list of numbers, use `.pack("f*")` to convert them into the compact BLOB format that `sqlite-vec` uses.

```ruby
embedding = [0.1, 0.2, 0.3, 0.4]
result = db.execute("SELECT vec_length(?)", [query.pack("f*")]])
puts result.first.first # 4
```
