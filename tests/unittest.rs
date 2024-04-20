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

#[link(name = "sqlite-vec-internal")]
extern "C" {
    fn min_idx(distances: *const f32, n: i32, out: *mut i32, k: i32) -> i32;
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
}
