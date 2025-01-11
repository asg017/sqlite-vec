.load dist/vec0


create virtual table vec_items using vec0(
  vector float[1]
);

insert into vec_items(rowid, vector)
  select value, json_array(value) from generate_series(1, 100);


select vec_to_json(vector), distance
from vec_items
where vector match '[1]'
  and k = 5;

select vec_to_json(vector), distance
from vec_items
where vector match '[1]'
  and k = 5
  and distance > 4.0;