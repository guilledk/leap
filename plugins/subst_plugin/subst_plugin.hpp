#pragma once

#include <eosio/vm/backend.hpp>

#include <eosio/chain/config.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/application.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/global_property_object.hpp>

#include "substitution_context.hpp"


#define DEFAULT_OVERRIDE_TIME 300
#define DEFAULT_MANIFEST_INTERVAL 300
#define DEFAULT_MANIFEST_TIMEOUT 5


namespace eosio {

    using chain::controller;
    using chainbase::database;

    class subst_plugin_impl;

    class subst_plugin : public appbase::plugin<subst_plugin> {
        public:
            APPBASE_PLUGIN_REQUIRES((chain_plugin))

            subst_plugin();
            virtual ~subst_plugin();

            void set_program_options(appbase::options_description& cli,
                                        appbase::options_description& cfg) override;
            void plugin_initialize(const appbase::variables_map& options);
            void plugin_startup();
            void plugin_shutdown();

            substitution_context& context();

        private:
            std::shared_ptr<subst_plugin_impl> my;
    };

}  // namespace eosio
