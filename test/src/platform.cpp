#include <gtest/gtest.h>

#include <SimpleHart.hpp>

#include <PhysicalMemory.hpp>

class PlatformFixture : public ::testing::Test {
protected:

    CASK::PhysicalMemory memory;
    SimpleHart hart;

    PlatformFixture() : hart(&memory) {
        
    }

    void SetUp() {

    }

    void TearDown() {

    }

    ~PlatformFixture() {

    }

};

TEST_F(PlatformFixture, ZeroRegIsZero) {
    EXPECT_EQ(hart.state.regs[0].Read32(), (__uint32_t)0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
