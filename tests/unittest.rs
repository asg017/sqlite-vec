fn main() {
    println!("Hello, world!");
    println!("{:?}", _min_idx(vec![3.0, 2.0, 1.0], 2));
}

fn _min_idx(distances: Vec<f32>, k: i32) -> Vec<i32> {
    let mut out: Vec<i32> = vec![0; k as usize];

    unsafe {
        min_idx(
            distances.as_ptr().cast(),
            distances.len() as i32,
            out.as_mut_ptr(),
            k,
        );
    }
    out
}

fn _merge_sorted_lists(
    a: &Vec<f32>,
    a_rowids: &Vec<i64>,
    b: &Vec<f32>,
    b_rowids: &Vec<i64>,
    b_top_idx: &Vec<i32>,
    n: usize,
) -> (Vec<i64>, Vec<f32>) {
    let mut out_used: i64 = 0;
    let mut out: Vec<f32> = Vec::with_capacity(n);
    let mut out_rowids: Vec<i64> = Vec::with_capacity(n);
    unsafe {
        merge_sorted_lists(
            a.as_ptr().cast(),
            a_rowids.as_ptr().cast(),
            a.len() as i64,
            b.as_ptr().cast(),
            b_rowids.as_ptr().cast(),
            b_top_idx.as_ptr().cast(),
            b.len() as i64,
            out.as_ptr().cast(),
            out_rowids.as_ptr().cast(),
            n as i64,
            &mut out_used,
        );
        out.set_len(out_used as usize);
        out_rowids.set_len(out_used as usize);
    }

    (out_rowids, out)
}

#[link(name = "sqlite-vec-internal")]
extern "C" {
    fn min_idx(distances: *const f32, n: i32, out: *mut i32, k: i32) -> i32;

    fn merge_sorted_lists(
        a: *const f32,
        a_rowids: *const i64,
        a_length: i64,
        b: *const f32,
        b_rowids: *const i64,
        b_top_idx: *const i32,
        b_length: i64,
        out: *const f32,
        out_rowids: *const i64,
        out_length: i64,
        out_used: *mut i64,
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic() {
        assert_eq!(_min_idx(vec![1.0, 2.0, 3.0], 3), vec![0, 1, 2]);
        assert_eq!(_min_idx(vec![3.0, 2.0, 1.0], 3), vec![2, 1, 0]);

        assert_eq!(_min_idx(vec![1.0, 2.0, 3.0], 2), vec![0, 1]);
        assert_eq!(_min_idx(vec![3.0, 2.0, 1.0], 2), vec![2, 1]);
    }

    #[test]
    fn test_merge_sorted_lists() {
        let a = &vec![0.01, 0.02, 0.03];
        let a_rowids = &vec![1, 2, 3];

        //let b = &vec![0.1, 0.2, 0.3, 0.4];
        //let b_rowids = &vec![4, 5, 6, 7];
        let b = &vec![0.4, 0.2, 0.3, 0.1];
        let b_rowids = &vec![7, 5, 6, 4];
        let b_top_idx = &vec![3, 1, 2, 0];

        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 0),
            (vec![], vec![])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 1),
            (vec![1], vec![0.01])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 2),
            (vec![1, 2], vec![0.01, 0.02])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 3),
            (vec![1, 2, 3], vec![0.01, 0.02, 0.03])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 4),
            (vec![1, 2, 3, 4], vec![0.01, 0.02, 0.03, 0.1])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 5),
            (vec![1, 2, 3, 4, 5], vec![0.01, 0.02, 0.03, 0.1, 0.2])
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 6),
            (
                vec![1, 2, 3, 4, 5, 6],
                vec![0.01, 0.02, 0.03, 0.1, 0.2, 0.3]
            )
        );
        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 7),
            (
                vec![1, 2, 3, 4, 5, 6, 7],
                vec![0.01, 0.02, 0.03, 0.1, 0.2, 0.3, 0.4]
            )
        );

        assert_eq!(
            _merge_sorted_lists(a, a_rowids, b, b_rowids, b_top_idx, 8),
            (
                vec![1, 2, 3, 4, 5, 6, 7],
                vec![0.01, 0.02, 0.03, 0.1, 0.2, 0.3, 0.4]
            )
        );
    }
    /*
    #[test]
    fn test_merge_sorted_lists_empty() {
        let x = vec![0.1, 0.2, 0.3];
        let x_rowids = vec![666, 888, 777];
        assert_eq!(
            _merge_sorted_lists(&x, &x_rowids, &vec![], &vec![], 3),
            (vec![666, 888, 777], vec![0.1, 0.2, 0.3])
        );
        assert_eq!(
            _merge_sorted_lists(&vec![], &vec![], &x, &x_rowids, 3),
            (vec![666, 888, 777], vec![0.1, 0.2, 0.3])
        );
        assert_eq!(
            _merge_sorted_lists(&vec![], &vec![], &x, &x_rowids, 4),
            (vec![666, 888, 777], vec![0.1, 0.2, 0.3])
        );
        assert_eq!(
            _merge_sorted_lists(&vec![], &vec![], &x, &x_rowids, 2),
            (vec![666, 888], vec![0.1, 0.2])
        );
    }*/
}
