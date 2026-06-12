// Benchmarks for the SIMD distance-dispatch paths (issue #302).
//
// Two benches:
//   distance/l2_float_d1536  — calls vec_distance_l2() directly via SQL scalar
//                               function; one distance computation per iteration,
//                               no KNN planner overhead. Tightest proxy for the
//                               AVX2 l2_sqr_float_avx kernel.
//   knn/n5000_d1536          — end-to-end KNN query over 5 000 vectors at d=1536.
//                               Setup is paid once outside b.iter; each iteration
//                               is a single query that exercises the distance
//                               dispatch loop ~5 000 times.
//
// Run:
//   cargo bench --bench simd_dispatch
//
// To capture a baseline before the SIMD dispatch fix:
//   cargo bench --bench simd_dispatch 2>&1 | tee /tmp/bench-before-simd.txt
// After applying the fix, run again; Criterion will print a regression/improvement
// line for each bench.

use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion};
use micro::init_vec;
use rand::Rng;
use rusqlite::Connection;
use zerocopy::AsBytes;

fn random_vector(n: usize) -> Vec<f32> {
    let mut rng = rand::thread_rng();
    (0..n).map(|_| rng.gen()).collect()
}

fn setup_knn_db(d: usize, n: usize) -> Connection {
    let mut db = Connection::open_in_memory().unwrap();
    db.execute(
        format!("create virtual table v using vec0(a float[{d}])").as_str(),
        [],
    )
    .unwrap();
    let tx = db.transaction().unwrap();
    for _ in 0..n {
        let vec = random_vector(d);
        tx.execute("insert into v(a) values (?)", [vec.as_bytes()])
            .unwrap();
    }
    tx.commit().unwrap();
    db
}

fn bench_distance_l2(c: &mut Criterion) {
    init_vec();
    let db = Connection::open_in_memory().unwrap();
    let a = random_vector(1536);
    let b = random_vector(1536);
    let mut stmt = db.prepare("select vec_distance_l2(?, ?)").unwrap();

    let mut group = c.benchmark_group("distance");
    group.bench_function("l2_float_d1536", |bench| {
        bench.iter(|| {
            let _: f64 = stmt
                .query_row(rusqlite::params![a.as_bytes(), b.as_bytes()], |r| r.get(0))
                .unwrap();
        });
    });
    group.finish();
}

fn bench_knn(c: &mut Criterion) {
    init_vec();
    let d = 1536;
    let n = 5_000;
    let k = 10;

    let mut group = c.benchmark_group("knn");
    for page_size in [4096usize, 8192, 16384] {
        let db = setup_knn_db(d, n);
        let query = random_vector(d);
        let mut stmt = db
            .prepare("select rowid, distance from v where a match ? order by distance limit ?")
            .unwrap();

        group.bench_with_input(
            BenchmarkId::new(format!("n{n}_d{d}"), page_size),
            &page_size,
            |b, _| {
                b.iter(|| {
                    let results: Vec<(i64, f64)> = stmt
                        .query_map(rusqlite::params![query.as_bytes(), k], |r| {
                            Ok((r.get(0).unwrap(), r.get(1).unwrap()))
                        })
                        .unwrap()
                        .collect::<Result<Vec<_>, _>>()
                        .unwrap();
                    assert_eq!(results.len(), k);
                });
            },
        );
    }
    group.finish();
}

criterion_group!(benches, bench_distance_l2, bench_knn);
criterion_main!(benches);
