add_library(
    subst_plugin

    substitution_context.cpp
    subst_plugin.cpp
)

target_link_libraries(
    subst_plugin
    chain_plugin
    http_plugin
    eosio_chain
    appbase
    fc)

target_include_directories(
    subst_plugin
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/abieos/include"
)
