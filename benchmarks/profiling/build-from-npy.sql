.timer on
pragma page_size = 32768;
--pragma page_size = 16384;
--pragma page_size = 16384;
--pragma page_size = 4096;

create virtual table vec_items using vec0(
  embedding float[1536]
);

-- 65s (limit 1e5), ~615MB on disk
insert into vec_items
  select
    rowid,
    vector
  from vec_npy_each(vec_npy_file('examples/dbpedia-openai/data/vectors.npy'))
  limit 1e5;
