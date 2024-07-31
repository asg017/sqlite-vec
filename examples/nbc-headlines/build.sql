.mode box
.header on
.bail on

.load ./vec0
.load ./rembed0

insert into rembed_clients(name, options)
 values ('llamafile', 'llamafile');

create table articles as 
select 
  value ->> 'url' as url,
  value ->> 'headline' as headline,
  rembed('llamafile', value ->> 'headline') as headline_embedding
from json_each(
  readfile('2024-07-26.json')
);

select writefile(
  'articles.json',
  json_group_array(
    json_object(
      'id', rowid,
      'url', url,
      'headline', headline,
      'headline_embedding', vec_to_json(headline_embedding)
    )
  )
)
from articles;
