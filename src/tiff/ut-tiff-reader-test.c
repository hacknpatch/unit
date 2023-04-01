#include "ut.h"

int main(int argc, char **argv) {
  UtObjectRef data = ut_uint8_list_new_from_hex_string(
      "49492a0038000000000000ffffff000000ffffffffffff000000ffffff000000000000ff"
      "ffff000000ffffffffffff000000ffffff00000011000001030001000000040000000101"
      "0300010000000400000002010300030000001a0100000301030001000000010000000601"
      "030001000000020000000e0102000b000000260100001101040001000000080000001201"
      "030001000000010000001501030001000000030000001601030001000000800000001701"
      "040001000000300000001a010500010000000a0100001b01050001000000120100001c01"
      "030001000000010000001d0102000b000000320100002801030001000000020000005301"
      "03000300000020010000000000002c010000010000002c01000001000000080008000800"
      "0100010001005465737420496d61676500004261636b67726f756e6400");
  UtObjectRef reader = ut_tiff_reader_new(data);
  UtObject *tags = ut_tiff_reader_get_tags(reader);
  ut_assert_non_null(tags);
  ut_assert_int_equal(ut_tiff_reader_get_image_width(reader), 4);
  ut_assert_int_equal(ut_tiff_reader_get_image_length(reader), 4);
  ut_cstring_ref description = ut_tiff_reader_get_image_description(reader);
  ut_assert_cstring_equal(description, "Test Image");

  return 0;
}
