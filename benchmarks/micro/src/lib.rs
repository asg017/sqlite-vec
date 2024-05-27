#![feature(test)]

extern crate test;

pub fn add_two(a: i32) -> i32 {
    a + 2
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::Connection;
    use test::Bencher;

    #[test]
    fn it_works() {
        assert_eq!(4, add_two(2));
    }

    #[bench]
    fn bench_add_two(b: &mut Bencher) {
        let db = Connection::open_in_memory().unwrap();
        let v: Vec<f32> = vec![0.1, 0.2, 0.3];

        b.iter(|| {
            let sqlite_version: String = db
                .query_row("select sqlite_version()", [], |x| x.get(0))
                .unwrap();
        });
    }
}
