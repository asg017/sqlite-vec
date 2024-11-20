
.load dist/vec0main
.bail on

.mode qbox


.load ./memstat
.echo on

select name, value from sqlite_memstat where name = 'MEMORY_USED';

create virtual table v using vec0(
  vector float[1],
  name1 text,
  name2 text,
  age int,
  chunk_size=8
);

select name, value from sqlite_memstat where name = 'MEMORY_USED';

insert into v(vector, name1, name2, age) values
  ('[1]', 'alex', 'xxxx', 1),
  ('[2]', 'alex', 'aaaa', 2),
  ('[3]', 'alex', 'aaaa', 3),
  ('[4]', 'brian', 'aaaa', 1),
  ('[5]', 'brian', 'aaaa', 2),
  ('[6]', 'brian', 'aaaa', 3),
  ('[7]', 'craig', 'aaaa', 1),
  ('[8]', 'craig', 'xxxx', 2),
  ('[9]', 'craig', 'xxxx', 3),
  ('[10]', '123456789012345', 'xxxx', 3);

select name, value from sqlite_memstat where name = 'MEMORY_USED';

select rowid, name1, name2, age, vec_to_json(vector)
from v
where vector match '[0]'
  and k = 5
  and name1 in ('alex', 'brian', 'craig')
  --and name2 in ('aaaa', 'xxxx')
  and age in (1, 2, 3, 2222,3333,4444);

select name, value from sqlite_memstat where name = 'MEMORY_USED';

select rowid, name1, name2, age, vec_to_json(vector)
from v
where vector match '[0]'
  and k = 5
  and name1 in ('123456789012345', 'superfluous');


.exit

create virtual table v using vec0(
  vector float[1],
  +description text
);
insert into v(rowid, vector, description) values (1, '[1]', 'aaa');
select * from v;

.exit

create virtual table vec_articles using vec0(
  article_id integer primary key,
  year integer partition key,
  headline_embedding float[1],
  +headline text,
  +url text,
  word_count integer,
  print_section text,
  print_page integer,
  pub_date text,
);

insert into vec_articles values (1111, 2020, '[1]', 'headline', 'https://...', 200, 'A', 1, '2020-01-01');

select * from vec_articles;

.exit


create table movies(movie_id integer primary key, synopsis text);
INSERT INTO movies(movie_id, synopsis)
VALUES
  (1, 'A family is haunted by demonic spirits after moving into a new house, requiring the help of paranormal investigators.'),
  (2, 'Two dim-witted friends embark on a cross-country road trip to return a briefcase full of money to its owner.'),
  (3, 'A team of explorers travels through a wormhole in space in an attempt to ensure humanity’s survival.'),
  (4, 'A young hobbit embarks on a journey with a fellowship to destroy a powerful ring and save Middle-earth from darkness.'),
  (5, 'A documentary about the dangers of global warming, featuring former U.S. Vice President Al Gore.'),
  (6, 'After the death of her secretive mother, a woman discovers terrifying secrets about her family lineage.'),
  (7, 'A clueless but charismatic TV anchorman struggles to stay relevant in the world of broadcast journalism.'),
  (8, 'A young blade runner uncovers a long-buried secret that leads him to track down former blade runner Rick Deckard.'),
  (9, 'A young boy discovers he is a wizard and attends a magical school, where he learns about his destiny.'),
  (10, 'A rock climber attempts to scale El Capitan in Yosemite National Park without the use of ropes or safety gear.'),
  (11, 'A young African-American man uncovers a disturbing secret when he visits his white girlfriend''s family estate.'),
  (12, 'Three friends wake up from a bachelor party in Las Vegas with no memory of the previous night and must retrace their steps.'),
  (13, 'A computer hacker learns about the true nature of his reality and his role in the war against its controllers.'),
  (14, 'In post-Civil War Spain, a young girl escapes into an eerie but captivating fantasy world.'),
  (15, 'A documentary that explores racial inequality in the United States, focusing on the prison system and mass incarceration.'),
  (16, 'A young woman is followed by an unknown supernatural force after a sexual encounter.'),
  (17, 'Two immature but well-meaning stepbrothers become instant rivals when their single parents marry.'),
  (18, 'A thief with the ability to enter people''s dreams is tasked with planting an idea into a target''s subconscious.'),
  (19, 'A mute woman forms a unique relationship with a mysterious aquatic creature being held in a secret research facility.'),
  (20, 'A documentary about the life and legacy of Fred Rogers, the beloved host of the children''s TV show "Mister Rogers'' Neighborhood."');


create virtual table vec_movies using vec0(
  movie_id integer primary key,
  synopsis_embedding float[1],
  +title text,
  genre text,
  num_reviews int,
  mean_rating float,
  chunk_size=8
);

.schema
/*
insert into vec_movies(movie_id, synopsis_embedding, num_reviews, mean_rating) values
  (1, '[1]', 153, 4.6),
  (2, '[2]', 382, 2.6),
  (3, '[3]', 53, 5.0),
  (4, '[4]', 210, 4.2),
  (5, '[5]', 93, 3.4),
  (6, '[6]', 167, 4.7),
  (7, '[7]', 482, 2.9),
  (8, '[8]', 301, 5.0),
  (9, '[9]', 134, 4.1),
  (10, '[10]', 66, 3.2),
  (11, '[11]', 88, 4.9),
  (12, '[12]', 59, 2.8),
  (13, '[13]', 423, 4.5),
  (14, '[14]', 275, 3.6),
  (15, '[15]', 191, 4.4),
  (16, '[16]', 314, 4.3),
  (17, '[17]', 74, 3.0),
  (18, '[18]', 201, 5.0),
  (19, '[19]', 399, 2.7),
  (20, '[20]', 186, 4.8);
*/

/*

INSERT INTO vec_movies(movie_id, synopsis_embedding, genre, num_reviews, mean_rating)
VALUES
  (1, '[1]', 'horror', 153, 4.6),
  (2, '[2]', 'comedy', 382, 2.6),
  (3, '[3]', 'scifi', 53, 5.0),
  (4, '[4]', 'fantasy', 210, 4.2),
  (5, '[5]', 'documentary', 93, 3.4),
  (6, '[6]', 'horror', 167, 4.7),
  (7, '[7]', 'comedy', 482, 2.9),
  (8, '[8]', 'scifi', 301, 5.0),
  (9, '[9]', 'fantasy', 134, 4.1),
  (10, '[10]', 'documentary', 66, 3.2),
  (11, '[11]', 'horror', 88, 4.9),
  (12, '[12]', 'comedy', 59, 2.8),
  (13, '[13]', 'scifi', 423, 4.5),
  (14, '[14]', 'fantasy', 275, 3.6),
  (15, '[15]', 'documentary', 191, 4.4),
  (16, '[16]', 'horror', 314, 4.3),
  (17, '[17]', 'comedy', 74, 3.0),
  (18, '[18]', 'scifi', 201, 5.0),
  (19, '[19]', 'fantasy', 399, 2.7),
  (20, '[20]', 'documentary', 186, 4.8);
*/

INSERT INTO vec_movies(movie_id, synopsis_embedding, genre, title, num_reviews, mean_rating)
VALUES
  (1, '[1]', 'horror', 'The Conjuring', 153, 4.6),
  (2, '[2]', 'comedy', 'Dumb and Dumber', 382, 2.6),
  (3, '[3]', 'scifi', 'Interstellar', 53, 5.0),
  (4, '[4]', 'fantasy', 'The Lord of the Rings: The Fellowship of the Ring', 210, 4.2),
  (5, '[5]', 'documentary', 'An Inconvenient Truth', 93, 3.4),
  (6, '[6]', 'horror', 'Hereditary', 167, 4.7),
  (7, '[7]', 'comedy', 'Anchorman: The Legend of Ron Burgundy', 482, 2.9),
  (8, '[8]', 'scifi', 'Blade Runner 2049', 301, 5.0),
  (9, '[9]', 'fantasy', 'Harry Potter and the Sorcerer''s Stone', 134, 4.1),
  (10, '[10]', 'documentary', 'Free Solo', 66, 3.2),
  (11, '[11]', 'horror', 'Get Out', 88, 4.9),
  (12, '[12]', 'comedy', 'The Hangover', 59, 2.8),
  (13, '[13]', 'scifi', 'The Matrix', 423, 4.5),
  (14, '[14]', 'fantasy', 'Pan''s Labyrinth', 275, 3.6),
  (15, '[15]', 'documentary', '13th', 191, 4.4),
  (16, '[16]', 'horror', 'It Follows', 314, 4.3),
  (17, '[17]', 'comedy', 'Step Brothers', 74, 3.0),
  (18, '[18]', 'scifi', 'Inception', 201, 5.0),
  (19, '[19]', 'fantasy', 'The Shape of Water', 399, 2.7),
  (20, '[20]', 'documentary', 'Won''t You Be My Neighbor?', 186, 4.8),
  (21, '[21]', 'scifi', 'Gravity', 342, 4.0),
  (22, '[22]', 'scifi', 'Dune', 451, 4.4),
  (23, '[23]', 'scifi', 'The Martian', 522, 4.6),
  (24, '[24]', 'horror', 'A Quiet Place', 271, 4.3),
  (25, '[25]', 'fantasy', 'The Chronicles of Narnia: The Lion, the Witch and the Wardrobe', 310, 3.9);

--select * from vec_movies;
--select * from vec_movies_metadata_chunks00;


create virtual table vec_chunks using vec0(
  user_id integer partition key,
  +contents text,
  contents_embedding float[1],
);

INSERT INTO vec_chunks (rowid, user_id, contents, contents_embedding) VALUES
(1, 123, 'Our PTO policy allows employees to take both vacation and sick leave as needed.', '[1]'),
(2, 123, 'Employees must provide notice at least two weeks in advance for planned vacations.', '[2]'),
(3, 123, 'Sick leave can be taken without advance notice, but employees must inform their manager.', '[3]'),
(4, 123, 'Unused PTO can be carried over to the following year, up to a maximum of 40 hours.', '[4]'),
(5, 123, 'PTO must be used in increments of at least 4 hours.', '[5]'),
(6, 456, 'New employees are granted 10 days of PTO during their first year of employment.', '[6]'),
(7, 456, 'After the first year, employees earn an additional day of PTO for each year of service.', '[7]'),
(8, 789, 'PTO requests will be reviewed by the HR department and are subject to approval.', '[8]'),
(9, 789, 'The company reserves the right to deny PTO requests during peak operational periods.', '[9]'),
(10, 456, 'If PTO is denied, the employee will be given an alternative time to take leave.', '[10]'),
(11, 789, 'Employees who are out of PTO must request unpaid leave for any additional time off.', '[11]'),
(12, 789, 'In case of a family emergency, employees can request emergency leave.', '[12]'),
(13, 456, 'Emergency leave may be granted for personal or family illness, or other critical situations.', '[13]'),
(14, 789, 'The maximum length of emergency leave is subject to company discretion.', '[14]'),
(15, 123, 'All PTO balances will be displayed on the employee self-service portal.', '[15]'),
(16, 456, 'Employees who are terminated will be paid for unused PTO, as per state law.', '[16]'),
(17, 123, 'Part-time employees are eligible for PTO on a pro-rata basis.', '[17]'),
(18, 789, 'The company encourages employees to use their PTO to maintain work-life balance.', '[18]'),
(19, 456, 'Employees should not book travel plans until their PTO request has been approved.', '[19]'),
(20, 123, 'Managers are responsible for tracking their team members'' PTO usage.', '[20]');

select rowid, user_id, contents, distance
from vec_chunks
where contents_embedding match '[19]'
  and user_id = 123
  and k = 5;

.exit





-- PARTITION KEY and auxiliar columns!
create virtual table vec_chunks using vec0(
  -- internally shard the vector index by user
  user_id integer partition key,
  -- store the chunk text pre-embedding as an "auxiliary column"
  +contents text,
  contents_embeddings float[1024],
);

select rowid, user_id, contents, distance
from vec_chunks
where contents_embedding match '[...]'
  and user_id = 123
  and k = 5;
/*
┌───────┬─────────┬──────────────────────────────────────────────────────────────┬──────────┐
│ rowid │ user_id │                           contents                           │ distance │
├───────┼─────────┼──────────────────────────────────────────────────────────────┼──────────┤
│ 20    │ 123     │ 'Managers are responsible for tracking their team members''  │ 1.0      │
│       │         │ PTO usage.'                                                  │          │
├───────┼─────────┼──────────────────────────────────────────────────────────────┼──────────┤
│ 17    │ 123     │ 'Part-time employees are eligible for PTO on a pro-rata basi │ 2.0      │
│       │         │ s.'                                                          │          │
├───────┼─────────┼──────────────────────────────────────────────────────────────┼──────────┤
│ 15    │ 123     │ 'All PTO balances will be displayed on the employee self-ser │ 4.0      │
│       │         │ vice portal.'                                                │          │
├───────┼─────────┼──────────────────────────────────────────────────────────────┼──────────┤
│ 5     │ 123     │ 'PTO must be used in increments of at least 4 hours.'        │ 14.0     │
├───────┼─────────┼──────────────────────────────────────────────────────────────┼──────────┤
│ 4     │ 123     │ 'Unused PTO can be carried over to the following year, up to │ 15.0     │
│       │         │  a maximum of 40 hours.'                                     │          │
└───────┴─────────┴──────────────────────────────────────────────────────────────┴──────────┘
*/





-- metadata filters!
create virtual table vec_movies using vec0(
  movie_id integer primary key,
  synopsis_embedding float[1024],
  genre text,
  num_reviews int,
  mean_rating float
);

select
  movie_id,
  title,
  genre,
  num_reviews,
  mean_rating,
  distance
from vec_movies
where synopsis_embedding match '[15.5]'
  and genre = 'scifi'
  and num_reviews between 100 and 500
  and mean_rating > 3.5
  and k = 5;
/*
┌──────────┬─────────────────────┬─────────┬─────────────┬──────────────────┬──────────┐
│ movie_id │        title        │  genre  │ num_reviews │   mean_rating    │ distance │
├──────────┼─────────────────────┼─────────┼─────────────┼──────────────────┼──────────┤
│ 13       │ 'The Matrix'        │ 'scifi' │ 423         │ 4.5              │ 2.5      │
│ 18       │ 'Inception'         │ 'scifi' │ 201         │ 5.0              │ 2.5      │
│ 21       │ 'Gravity'           │ 'scifi' │ 342         │ 4.0              │ 5.5      │
│ 22       │ 'Dune'              │ 'scifi' │ 451         │ 4.40000009536743 │ 6.5      │
│ 8        │ 'Blade Runner 2049' │ 'scifi' │ 301         │ 5.0              │ 7.5      │
└──────────┴─────────────────────┴─────────┴─────────────┴──────────────────┴──────────┘
*/




.exit

create virtual table vec_movies using vec0(
  movie_id integer primary key,
  synopsis_embedding float[768],
  genre text,
  num_reviews int,
  mean_rating float,
);


.exit


create virtual table vec_chunks using vec0(
  chunk_id integer primary key,
  contents_embedding float[1],
  +contents text
);
insert into vec_chunks(chunk_id, contents_embedding, contents) values
  (1, '[1]', 'alex'),
  (2, '[2]', 'brian'),
  (3, '[3]', 'craig'),
  (4, '[4]', 'dylan');

select * from vec_chunks;

select chunk_id, contents, distance
from vec_chunks
where contents_embedding match '[5]'
and k = 3;

.exit

create virtual table v using vec0(a float[1]);
select count(*) from v_chunks;
insert into v(a) values ('[1.11]');
select * from v;
drop table v;

create virtual table v using vec0(

  v_aaa float[1],
  partk_xxx int partition key,
  v_bbb float[2],
  partk_yyy text partition key,
  chunk_size=32
);


insert into v(rowid, v_aaa, partk_xxx, v_bbb, partk_yyy) values
  (1, '[.1]', 999, '[.11, .11]', 'alex'),
  (2, '[.2]', 999, '[.22, .22]', 'alex'),
  (3, '[.3]', 999, '[.33, .33]', 'brian');


select rowid, vec_to_json(v_aaa), partk_xxx, vec_to_json(v_bbb), partk_yyy from v;

select * from v;
select * from v where rowid = 2;
update v
set v_aaa = '[.222]',
  v_bbb = '[.222, .222]'
where rowid = 2;

select rowid, vec_to_json(v_aaa), partk_xxx, vec_to_json(v_bbb), partk_yyy from v;

select chunk_id, size, sequence_id, partition00, partition01, (validity), length(rowids) from v_chunks;

--explain query plan
select *, distance
from v
where v_aaa match '[.5]'
  and partk_xxx = 999
  and partk_yyy = 'alex'
  --and partk_xxx != 20
  and k = 5;
