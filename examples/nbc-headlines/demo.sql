.load vec0
.header on
.bail on
.timer on

create temp table raw_articles as
  select
    value ->> 'id' as id,
    value ->> 'url' as url,
    value ->> 'headline' as headline,
    value ->> 'headline_embedding' as headline_embedding
  from json_each(
    readfile('articles.json')
  );

create table articles(
  id integer primary key,
  headline text,
  url text
);

insert into articles(id, headline, url)
  select id, headline, url from temp.raw_articles;

select * from articles limit 5;

create virtual table vec_articles using vec0(
  article_id integer primary key,
  headline_embedding float[384]
);

insert into vec_articles(article_id, headline_embedding)
  select id, headline_embedding from temp.raw_articles;


select * from articles limit 5;
select article_id, vec_to_json(headline_embedding) from articles limit 5;

.param set :query 'sports'

.load ./rembed0
insert into rembed_clients values ('all-MiniLM-L6-v2', 'llamafile');


.mode qbox -ww

select
  article_id,
  --headline,
  distance
from vec_articles
--left join articles on articles.id = vec_articles.article_id
where headline_embedding match rembed('all-MiniLM-L6-v2', :query)
  and k = 10;
