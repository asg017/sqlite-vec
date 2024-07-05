.load dist/vec0
.mode box
.header on
.eqp on
.echo on

select sqlite_version(), vec_version();

select * from vec_each('[1,2,3]');

select *
from json_each('[
  [1,2,3,4],
  [1,2,3,4]
]')
join vec_each(json_each.value);
