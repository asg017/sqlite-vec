.load ../../dist/vec0
.mode box
.header on

select sqlite_version(), vec_version();

CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[8]);

INSERT INTO vec_items(rowid, embedding)
  select
    value ->> 0,
    value ->> 1
  from json_each('[
    [1, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]],
    [2, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]]
  ]');

SELECT
  rowid,
  distance
FROM vec_items
WHERE embedding MATCH '[0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]'
ORDER BY distance
LIMIT 5;
