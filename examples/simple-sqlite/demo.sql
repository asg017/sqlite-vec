.load ../../dist/vec0
.mode table
.header on

select sqlite_version(), vec_version();

CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4]);

INSERT INTO vec_items(rowid, embedding)
  select
    value ->> 0,
    value ->> 1
  from json_each('[
    [1, [0.1, 0.1, 0.1, 0.1]],
    [2, [0.2, 0.2, 0.2, 0.2]],
    [3, [0.3, 0.3, 0.3, 0.3]],
    [4, [0.4, 0.4, 0.4, 0.4]],
    [5, [0.5, 0.5, 0.5, 0.5]]
  ]');

SELECT
  rowid,
  distance
FROM vec_items
WHERE embedding MATCH '[0.3, 0.3, 0.3, 0.3]'
ORDER BY distance
LIMIT 3;
