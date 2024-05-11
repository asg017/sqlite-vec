#[link(name = "sqlite_vec0")]
extern "C" {
    pub fn sqlite3_vec_init();
}

#[cfg(test)]
mod tests {
    use super::*;

    use rusqlite::{ffi::sqlite3_auto_extension, Connection};

    #[test]
    fn test_rusqlite_auto_extension() {
        unsafe {
            sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
        }

        let conn = Connection::open_in_memory().unwrap();

        let result: String = conn
            .query_row("select vec_version()", [], |x| x.get(0))
            .unwrap();

        assert!(result.starts_with("v"));
    }
}
