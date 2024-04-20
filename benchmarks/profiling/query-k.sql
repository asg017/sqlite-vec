.timer on

select rowid, distance
from vec_items
where embedding match (select embedding from vec_items where rowid = 100)
  and k = :k
order by distance;

select rowid, distance
from vec_items
where embedding match (select embedding from vec_items where rowid = 100)
  and k = :k
order by distance;

select rowid, distance
from vec_items
where embedding match (select embedding from vec_items where rowid = 100)
  and k = :k
order by distance;

select rowid, distance
from vec_items
where embedding match (select embedding from vec_items where rowid = 100)
  and k = :k
order by distance;

select rowid, distance
from vec_items
where embedding match (select embedding from vec_items where rowid = 100)
  and k = :k
order by distance;
