.load ../../dist/vec0
.load ./rembed0
insert into rembed_clients(name, options)
  values ('snowflake-arctic-embed-m-v1.5', 'llamafile');

.bail on
.mode box
.header on
.timer on

.param set :query 'death row'
.param set :weight_fts 1.0
.param set :weight_vec 1.0
.param set :rrf_k 60
.param set :query_embedding "vec_normalize(vec_slice(rembed('snowflake-arctic-embed-m-v1.5', :query), 0, 256))"
.param set :k 10

select 'Hybrid w/ RRF' as "";

with vec_matches as (
  select
    article_id,
    row_number() over (order by distance) as rank_number,
    distance
  from vec_headlines
  where
    headline_embedding match :query_embedding
    and k = :k
  order by distance
),
fts_matches as (
  select
    rowid,
    --highlight(fts_headlines, 0, '<b>', '</b>') as headline_highlighted,
    row_number() over (order by rank) as rank_number,
    rank as score
  from fts_headlines
  where headline match :query
  limit :k
),
final as (
  select
    articles.id,
    articles.headline,
    vec_matches.distance as vector_distance,
    fts_matches.score as fts_score,
    coalesce(1.0 / (:rrf_k + fts_matches.rowid), 0.0) * :weight_fts +
      coalesce(1.0 / (:rrf_k + vec_matches.article_id), 0.0) * :weight_vec
      as combined_score

  from fts_matches
  full outer join vec_matches on vec_matches.article_id = fts_matches.rowid
  join articles on articles.rowid = coalesce(fts_matches.rowid, vec_matches.article_id)
  order by combined_score desc
)
select * from final;

