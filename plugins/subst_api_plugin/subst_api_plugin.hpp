#pragma once

#include <eosio/http_plugin/http_plugin.hpp>

#include "subst_api_plugin.hpp"
#include "subst_plugin.hpp"
#include "api.hpp"


namespace eosio {

    class subst_api_plugin : public appbase::plugin<subst_api_plugin> {
        public:
            APPBASE_PLUGIN_REQUIRES((subst_plugin)(http_plugin))

            subst_api_plugin();
            virtual ~subst_api_plugin();

            void set_program_options(appbase::options_description& cli,
                                        appbase::options_description& cfg) override;
            void plugin_initialize(const appbase::variables_map& options);
            void plugin_startup();
            void plugin_shutdown();
    };

}  // namespace eosio
