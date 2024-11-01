# Using `sqlite-vec` in rqlite

[rqlite](https://rqlite.io/) users can use `sqlite-vec` with rqlite by loading the extension when they launch their rqlite node:

```bash
# Download a sqlite-vec release.
curl -L https://github.com/asg017/sqlite-vec/releases/download/v0.1.1/sqlite-vec-0.1.1-loadable-linux-x86_64.tar.gz -o sqlite-vec.tar.gz

# Tell rqlite to load sqlite-vec at launch time.
rqlited -extensions-path=sqlite-vec.tar.gz data
```

Once loaded you can use `sqlite-vec` functionality within rqlite. For example, you can perform searches via the [rqlite shell](https://rqlite.io/docs/cli/):

```
$ rqlite
Welcome to the rqlite CLI.
Enter ".help" for usage hints.
Connected to http://127.0.0.1:4001 running version 8
127.0.0.1:4001> create virtual table vec_examples using vec0(sample_embedding float[8]);
1 row affected
127.0.0.1:4001> insert into vec_examples(rowid, sample_embedding) values (1, '[-0.200, 0.250, 0.341, -0.211, 0.645, 0.935, -0.316, -0.924]'), (2, '[0.443, -0.501, 0.355, -0.771, 0.707, -0.708, -0.185, 0.362]'), (3, '[0.716, -0.927, 0.134, 0.052, -0.669, 0.793, -0.634, -0.162]'), (4, '[-0.710, 0.330, 0.656, 0.041, -0.990, 0.726, 0.385, -0.958]')
4 rows affected
127.0.0.1:4001> select rowid, distance from vec_examples where sample_embedding match '[0.890, 0.544, 0.825, 0.961, 0.358, 0.0196, 0.521, 0.175]' order by distance limit 2
+-------+-------------------+
| rowid | distance          |
+-------+-------------------+
| 2     | 2.386873722076416 |
+-------+-------------------+
| 1     | 2.389785051345825 |
+-------+-------------------+
```

You can learn more from the [rqlite website](https://rqlite.io/docs/guides/extensions/).

