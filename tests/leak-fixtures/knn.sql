.load dist/vec0
.mode box
.header on
.eqp on
.echo on

select sqlite_version(), vec_version();

create virtual table v using vec0(a float[1], chunk_size=8);

insert into v
  select value, format('[%f]', value / 100.0)
  from generate_series(1, 100);

select
  rowid,
  vec_to_json(a)
from v
where a match '[.3]'
  and k = 2;

select
  rowid,
  vec_to_json(a)
from v
where a match '[.3]'
  and k = 0;


select
  rowid,
  vec_to_json(a)
from v
where a match '[2.0]'
  and k = 2
  and rowid in (1,2,3,4,5);



with queries as (
  select
    rowid as query_id,
    json_array(value / 100.0) as value
  from generate_series(24, 39)
)
select
  query_id,
  rowid,
  distance,
  vec_to_json(a)
from queries, v
where a match queries.value
  and k =5;


select *
from v
where rowid in (1,2,3,4);

drop table v;

