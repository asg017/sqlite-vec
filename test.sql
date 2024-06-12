.load dist/vec0
.mode box
.header on

create table test as
  select value
  from json_each('[
    [1.0, 2.0, -3.0],
    [-1.0, 2.0, 3.0],
    [1.0, 2.0, 3.0],
    [1.0, 2.0, 3.0],
    [1.0, -2.0, 3.0]
  ]');

select
  vec_to_json(vec_min(value)),
  vec_to_json(vec_max(value)),
  vec_to_json(vec_avg(value))
from test;


