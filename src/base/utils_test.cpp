#include <gtest/gtest.h>
#include "utils.h"

TEST(Utils, runing_in_main)
{
    EXPECT_EQ(cbox_utils_running_in_main(), 1);
    EXPECT_NE(cbox_utils_running_in_main(), 0);
}

TEST(Utils, hex_str_to_bin)
{
    unsigned char buff[8] = { 0 };
    cbox_utils_hexstr_to_bin("0001", buff);
    EXPECT_EQ(buff[0], 0x00);
    EXPECT_EQ(buff[1], 0x01);
    memset(buff, 0x00, sizeof(buff));
    cbox_utils_hexstr_to_bin("9A3B2CFF87", buff);
    EXPECT_EQ(buff[0], 0x9A);
    EXPECT_EQ(buff[1], 0x3B);
    EXPECT_EQ(buff[2], 0x2C);
    EXPECT_EQ(buff[3], 0xFF);
    EXPECT_EQ(buff[4], 0x87);
}
