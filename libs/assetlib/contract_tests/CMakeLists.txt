file(GLOB IMPORT_CONTRACT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(assetlib_import_contract_tests ${IMPORT_CONTRACT_SOURCES})
target_include_directories(assetlib_import_contract_tests PRIVATE ../include)
# The fake supplies AssetStore's methods; linking assetlib would replace the host under test.
target_link_libraries(assetlib_import_contract_tests PRIVATE assetlib_structs core Catch2::Catch2WithMain)
set_target_properties(assetlib_import_contract_tests PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
enable_strict_compiler(assetlib_import_contract_tests)
enable_coverage(assetlib_import_contract_tests)
