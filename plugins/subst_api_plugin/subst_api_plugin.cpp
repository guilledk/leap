#include "subst_api_plugin.hpp"

namespace eosio {

    static auto _subst_api_plugin = application::register_plugin<subst_api_plugin>();

    subst_api_plugin::subst_api_plugin() {}
    subst_api_plugin::~subst_api_plugin() {}

    void subst_api_plugin::set_program_options(options_description& cli, options_description& cfg) {
        auto options = cfg.add_options();
        options(
            "subst-admin-apis", bpo::bool_switch()->default_value(false),
            "Enable subst apis that perform metadata changes");
    }

    void subst_api_plugin::plugin_initialize(const variables_map& options) {
        try {
            auto* _subst = app().find_plugin<subst_plugin>();
            auto* _http_plugin = app().find_plugin<http_plugin>();

            subst_apis subst_api(
                _http_plugin->get_max_response_time(), _subst->context());

            // read only
            _http_plugin->add_api({

                SUBST_RO_CALL(status, 200, http_params_types::possible_no_params)

            }, appbase::exec_queue::read_only);

            // read-write
            if (options.at("subst-admin-apis").as<bool>()) {
                wlog("subst-admin-apis enabled, don\'t expose these to ");
                _http_plugin->add_api({

                    SUBST_RW_CALL(upsert,         200, http_params_types::params_required),
                    SUBST_RW_CALL(activate,       200, http_params_types::possible_no_params),
                    SUBST_RW_CALL(deactivate,     200, http_params_types::possible_no_params),
                    SUBST_RW_CALL(remove,         200, http_params_types::possible_no_params),

                    SUBST_RW_CALL(fetch_manifest, 200, http_params_types::no_params)

                }, appbase::exec_queue::read_write, appbase::priority::highest);
            }

        } FC_LOG_AND_RETHROW()
    }  // subst_api_plugin::plugin_initialize

    void subst_api_plugin::plugin_startup() {}

    void subst_api_plugin::plugin_shutdown() {}

}  // namespace eosio
