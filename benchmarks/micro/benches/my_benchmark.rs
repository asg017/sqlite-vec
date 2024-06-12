use criterion::{black_box, criterion_group, criterion_main, Criterion};
use micro::init_vec;
use rand::Rng;
use rusqlite::Connection;
use zerocopy::AsBytes;

fn random_vector(n: usize) -> Vec<f32> {
    let mut rng = rand::thread_rng();
    (0..n).map(|_| rng.gen()).collect()
}

fn setup_base(page_size: usize, d: usize, n: i32) -> Connection {
    let base: Vec<Vec<f32>> = (0..n).map(|_| random_vector(d)).collect();

    let mut db = Connection::open_in_memory().unwrap();
    db.pragma_update(
        Some(rusqlite::DatabaseName::Main),
        "page_size",
        page_size, //,
                   //|row| Ok(assert!(row.get::<usize, String>(0).unwrap() == page_size)),
    )
    .unwrap();
    assert_eq!(
        db.pragma_query_value(Some(rusqlite::DatabaseName::Main), "page_size", |v| {
            Ok(v.get::<usize, usize>(0).unwrap())
        })
        .unwrap(),
        page_size,
    );
    db.execute(
        format!("create virtual table vec_base using vec0(a float[{d}])").as_str(),
        [],
    )
    .unwrap();

    let tx = db.transaction().unwrap();
    for item in &base {
        tx.execute("insert into vec_base(a) values (?)", [item.as_bytes()])
            .unwrap();
    }
    tx.commit().unwrap();
    db
}
pub fn criterion_benchmark(c: &mut Criterion) {
    init_vec();

    let n = 1_000_000;
    let d = 1536;
    let k = 10;
    let page_size = 8192;

    let page_sizes = [4096, 8192, 16384, 32768];
    for page_size in page_sizes {
        let db = setup_base(page_size, d, n);

        let mut stmt = db
            .prepare("select rowid, a from vec_base where rowid = ?")
            .unwrap();

        c.bench_function(
            format!("point page_size={page_size} n={n} dimension={d} k={k}").as_str(),
            |b| {
                let mut rng = rand::thread_rng();
                let query: i64 = rng.gen_range(0..n.into());

                b.iter(|| {
                    let result: (i64, Vec<u8>) = stmt
                        .query_row(rusqlite::params![query], |r| {
                            Ok((r.get(0).unwrap(), r.get(1).unwrap()))
                        })
                        .unwrap();
                    assert_eq!(result.0, query);
                });
            },
        );
        /*
        c.bench_function(
            format!("KNN page_size={page_size} n={n} dimension={d} k={k}").as_str(),
            |b| {
                let query: Vec<f32> = random_vector(d);
                let db = setup_base(page_size, d, n);

                let mut stmt = db.prepare(
                "select rowid, distance from vec_base where a match ? order by distance limit ?",
                )
                .unwrap();

                b.iter(|| {
                    let result: Vec<(i64, f64)> = stmt
                        .query_map(rusqlite::params![query.as_bytes(), k], |r| {
                            Ok((r.get(0).unwrap(), r.get(1).unwrap()))
                        })
                        .unwrap()
                        .collect::<Result<Vec<_>, _>>()
                        .unwrap();
                    assert_eq!(result.len(), 10);
                });
                stmt.finalize().unwrap()
            },
        ); */
    }
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
