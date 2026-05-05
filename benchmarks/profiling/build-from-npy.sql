.timer on
pragma page_size = 32768;
--pragma page_size = 16384;
--pragma page_size = 16384;
--pragma page_size = 4096;

create virtual table vec_items using vec0(
  embedding float[1536]
);

