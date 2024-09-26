.open tmp.db
--.load ./vec0
.load ./lembed0
.timer on

insert into lembed_models(name, model)
  values (
    'default',
    lembed_model_from_file('all-MiniLM-L6-v2.e4ce9877.q8_0.gguf')
  );

with subset as (
  select headline from articles limit 1000
)
select sum(lembed(headline)) from subset;


.load ./rembed0

insert into rembed_clients(name, options)
  values ('default','llamafile');

with subset as (
  select headline from articles limit 1000
)
select sum(rembed('default', headline)) from subset;
