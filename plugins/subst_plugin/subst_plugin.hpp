#pragma once

#include <chrono>
#include <string>

#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include <fc/io/json.hpp>
#include <fc/network/url.hpp>
#include <fc/network/http/http_client.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/application.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/wasm_interface_private.hpp>

#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#include <eosio/chain/webassembly/runtime_interface.hpp>

#include <eosio/chain_plugin/chain_plugin.hpp>

#define TEMPORARY_FAILURE chain::eosvmoc::code_cache_base::get_cd_failure::temporary
#define PERMANENT_FAILURE chain::eosvmoc::code_cache_base::get_cd_failure::permanent

namespace http = boost::beast::http;

namespace eosio
{
   using chain::controller;
   typedef boost::filesystem::path bpath;

   struct subst_plugin_impl;

   class subst_plugin : public appbase::plugin<subst_plugin>
   {
     public:
      APPBASE_PLUGIN_REQUIRES((chain_plugin))

      subst_plugin();
      virtual ~subst_plugin();

      void set_program_options(appbase::options_description& cli,
                               appbase::options_description& cfg) override;
      void plugin_initialize(const appbase::variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

     private:
      std::shared_ptr<subst_plugin_impl> my;
   };
}  // namespace eosio
