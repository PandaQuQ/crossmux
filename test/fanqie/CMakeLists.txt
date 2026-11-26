add_executable(FanqieProtocolTest
  FanqieCryptoTest.cpp
  FanqieProtocolTest.cpp
  FanqieStoreTest.cpp
  ${REPO_ROOT}/lib/FanqieNovel/src/FanqieCrypto.cpp
  ${REPO_ROOT}/lib/FanqieNovel/src/FanqieProtocol.cpp
  ${REPO_ROOT}/lib/FanqieNovel/src/FanqieStore.cpp
  ${REPO_ROOT}/lib/FanqieNovel/src/FanqieEpub.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(FanqieProtocolTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/FanqieNovel/src
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
)

target_compile_definitions(FanqieProtocolTest PRIVATE
  FANQIE_NOVEL=1
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(FanqieProtocolTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FanqieProtocolTest PROPERTIES RUN_SERIAL TRUE)
