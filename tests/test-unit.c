#include "../sqlite-vec.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

void test_vec0_parse_partition_key_definition() {
  printf("Starting %s...\n", __func__);
  typedef struct {
    char * test;
    int expected_rc;
    const char *expected_column_name;
    int expected_column_type;
  } TestCase;

  TestCase suite[] = {
    {"user_id integer partition key", SQLITE_OK, "user_id", SQLITE_INTEGER},
    {"USER_id int partition key", SQLITE_OK, "USER_id", SQLITE_INTEGER},
    {"category text partition key", SQLITE_OK, "category", SQLITE_TEXT},

    {"", SQLITE_EMPTY, "", 0},
    {"document_id text primary key", SQLITE_EMPTY, "", 0},
    {"document_id text partition keyy", SQLITE_EMPTY, "", 0},
  };
  for(int i = 0; i < countof(suite); i++) {
    char * out_column_name;
    int out_column_name_length;
    int out_column_type;
    int rc;
    rc = vec0_parse_partition_key_definition(
      suite[i].test,
      strlen(suite[i].test),
      &out_column_name,
      &out_column_name_length,
      &out_column_type
    );
    printf("2\n");
    assert(rc == suite[i].expected_rc);

    if(rc == SQLITE_OK) {
      assert(out_column_name_length == strlen(suite[i].expected_column_name));
      assert(strncmp(out_column_name, suite[i].expected_column_name, out_column_name_length) == 0);
      assert(out_column_type == suite[i].expected_column_type);
    }

    printf("✅ %s\n", suite[i].test);
  }
}

int main() {
  printf("Starting unit tests...\n");
  test_vec0_parse_partition_key_definition();
}
