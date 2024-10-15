
.load dist/vec0
.echo on
.bail on

.mode qbox

create virtual table vec_chunks using vec0(
  chunk_id integer primary key,
  contents_embedding float[1],
  +contents text
);
insert into vec_chunks(chunk_id, contents_embedding, contents) values
  (1, '[1]', 'alex'),
  (2, '[2]', 'brian'),
  (3, '[3]', 'craig'),
  (4, '[4]', 'dylan');

select * from vec_chunks;

select chunk_id, contents, distance
from vec_chunks
where contents_embedding match '[5]'
and k = 3;

.exit

create virtual table v using vec0(a float[1]);
select count(*) from v_chunks;
insert into v(a) values ('[1.11]');
select * from v;
drop table v;

create virtual table v using vec0(

  v_aaa float[1],
  partk_xxx int partition key,
  v_bbb float[2],
  partk_yyy text partition key,
  chunk_size=32
);


insert into v(rowid, v_aaa, partk_xxx, v_bbb, partk_yyy) values
  (1, '[.1]', 999, '[.11, .11]', 'alex'),
  (2, '[.2]', 999, '[.22, .22]', 'alex'),
  (3, '[.3]', 999, '[.33, .33]', 'brian');


select rowid, vec_to_json(v_aaa), partk_xxx, vec_to_json(v_bbb), partk_yyy from v;

select * from v;
select * from v where rowid = 2;
update v
set v_aaa = '[.222]',
  v_bbb = '[.222, .222]'
where rowid = 2;

select rowid, vec_to_json(v_aaa), partk_xxx, vec_to_json(v_bbb), partk_yyy from v;

select chunk_id, size, sequence_id, partition00, partition01, (validity), length(rowids) from v_chunks;

--explain query plan
select *, distance
from v
where v_aaa match '[.5]'
  and partk_xxx = 999
  and partk_yyy = 'alex'
  --and partk_xxx != 20
  and k = 5;
