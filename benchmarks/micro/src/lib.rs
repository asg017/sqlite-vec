use rusqlite::ffi::sqlite3_auto_extension;

#[link(name = "sqlite_vec0")]
extern "C" {
    pub fn sqlite3_vec_init();
}

pub fn init_vec() {
    unsafe {
        sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
    }
}
