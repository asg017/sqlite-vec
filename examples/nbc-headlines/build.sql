.mode box
.header on
.bail on

begin;

create virtual table fts_headlines using fts5(
  headline,
  content='articles', content_rowid='id'
);

insert into fts_headlines(rowid, headline)
  select rowid, headline
  from articles;

INSERT INTO fts_headlines(fts_headlines) VALUES('optimize');

.timer on

.load ../../dist/vec0
.load ./lembed0

insert into lembed_models(name, model) values
  ('default', lembed_model_from_file('all-MiniLM-L6-v2.e4ce9877.q8_0.gguf'));

create virtual table vec_headlines using vec0(
  article_id integer primary key,
  headline_embedding float[384]
);

-- 1m23s
insert into vec_headlines(article_id, headline_embedding)
select
  rowid,
  lembed(headline)
from articles;
commit;


-- rembed vec0 INSERT: 10m17s
-- before:                        4.37 MB
-- /w fts content:                5.35 MB (+0.98 MB)
--    with optimize               5.30 MB (-0.049 MB)
-- w/ fts:                        6.67 MB (+2.30 MB)
-- sum(octet_length(headline)):   1.16 MB
