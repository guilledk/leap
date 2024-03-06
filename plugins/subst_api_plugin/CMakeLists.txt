add_library(
    subst_api_plugin

    subst_api_plugin.cpp
)

target_link_libraries(
    subst_api_plugin
    subst_plugin
    http_plugin
    eosio_chain
    appbase
    fc)

target_include_directories(
    subst_api_plugin
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../subst_plugin"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/abieos/include"
)
