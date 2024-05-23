#include "subst_plugin.hpp"

namespace eosio {

    static auto _subst_plugin = application::register_plugin<subst_plugin>();

    class subst_plugin_impl : public std::enable_shared_from_this<subst_plugin_impl> {
        private:
            boost::asio::steady_timer manifest_timer;

        public:
            subst_plugin_impl(boost::asio::io_service& io)
            : manifest_timer(io)
            {}

        substitution_context* subst_ctx;
        chainbase::database* db;
        controller* control;

        appbase::variables_map app_options;

        bool override_tx_time = false;
        bool should_perform_override = false;
        uint32_t override_time = DEFAULT_OVERRIDE_TIME;
        uint32_t manifest_interval = DEFAULT_MANIFEST_INTERVAL;
        uint32_t manifest_timeout = DEFAULT_MANIFEST_TIMEOUT;

        void init(chain_plugin* chain, const variables_map& options) {
            app_options = options;

            control = &chain->chain();
            db = &control->mutable_db();
            subst_ctx = new substitution_context(control);

            manifest_interval = app_options.at("subst-manifest-interval").as<uint32_t>();
            manifest_timeout = app_options.at("subst-manifest-timeout").as<uint32_t>();

            control->get_wasm_interface().substitute_apply = [&](
                const chain::digest_type& code_hash,
                uint8_t vm_type, uint8_t vm_version,
                chain::apply_context& context
            ) {
                try {
                    name receiver = context.get_receiver();
                    auto act = context.get_action();

                    // gpo override hook
                    if (override_tx_time) {
                        if (should_perform_override) pwn_gpo();

                        if (receiver == name("eosio") &&
                            act.name == name("setparams")) {

                            should_perform_override = true;
                            ilog(
                                "setparams detected at ${bnum}, pwning gpo on next action",
                                ("bnum", control->pending_block_num())
                            );
                        }
                    }

                    // substitution hook
                    subst_ctx->apply_hook(code_hash, vm_type, vm_version, context);

                    return false;
                } FC_LOG_AND_RETHROW()
            };

            control->post_db_init = [&]() {
                post_db_init();
            };

            ilog("installed substitution hook for ${cid}", ("cid", control->get_chain_id()));
        }

        void post_db_init(){
            db->add_index<subst_meta_index>();

            override_tx_time = (app_options.count("override-max-tx-time") &&
                                        app_options["override-max-tx-time"].as<uint32_t>());

            should_perform_override = override_tx_time;

            if (should_perform_override) {
                override_time = app_options["override-max-tx-time"].as<uint32_t>();

                ilog("should_perform_override: ${over}ms", ("over",override_time));
            }

            if (app_options.count("subst-by-name")) {
                auto substs = app_options.at("subst-by-name").as<vector<string>>();
                for (auto& s : substs) {
                    std::vector<std::string> v;
                    boost::split(v, s, boost::is_any_of(":"));

                    EOS_ASSERT(
                        v.size() == 2,
                        fc::invalid_arg_exception,
                        "Invalid value ${s} for --subst-by-name"
                        " format is ${account_name}:${path_to_wasm}", ("s", s)
                    );

                    auto sinfo = v[0];
                    auto new_code_path = v[1];

                    std::vector<uint8_t> new_code = eosio::vm::read_wasm(new_code_path);
                    subst_ctx->upsert(sinfo, new_code);
                }
            }
            string manifest_str = app_options.at("subst-manifest").as<string>();
            if (manifest_str != "") {
                fc::url manifest_url = fc::url(manifest_str);
                EOS_ASSERT(
                    manifest_url.proto() == "http",
                    fc::invalid_arg_exception,
                    "Only http protocol supported for now."
                );
                subst_ctx->manifest_url = manifest_url;
                subst_ctx->fetch_manifest(fc::seconds(manifest_timeout));
                schedule_manifest_update();
            }

            subst_ctx->debug_print();
        }

        void schedule_manifest_update() {
            ilog("scheduling manifest update in ${i}", ("i",manifest_interval));
            manifest_timer.expires_from_now(std::chrono::seconds(manifest_interval));
            manifest_timer.async_wait(
                app().executor().wrap(
                    priority::high,
                    exec_queue::read_write,
                    [weak_this = weak_from_this()]( const boost::system::error_code& ec ) {
                        auto self = weak_this.lock();
                        if(self && ec != boost::asio::error::operation_aborted) {
                            ilog("trigger manifest update");
                            try {
                                 self->subst_ctx->fetch_manifest(fc::seconds(self->manifest_timeout));
                            } FC_LOG_AND_DROP();
                            self->schedule_manifest_update();
                        }
                    }
                )
            );
        }

        void pwn_gpo() {
            const auto& gpo = control->get_global_properties();
            const auto override_time_us = override_time * 1000;
            const auto max_block_cpu_usage = gpo.configuration.max_transaction_cpu_usage;
            // auto pwnd_options = prod_plug->get_runtime_options();
            db->modify(gpo, [&](auto& dgp) {
                // pwnd_options.max_transaction_time = override_time;
                dgp.configuration.max_transaction_cpu_usage = override_time_us;
                ilog(
                    "new max_trx_cpu_usage value: ${pwnd_value}",
                    ("pwnd_value", gpo.configuration.max_transaction_cpu_usage)
                );
                if (override_time_us > max_block_cpu_usage) {
                    ilog(
                        "override_time (${otime}us) is > max_block_cpu_usage (${btime}us), overriding as well",
                        ("otime", override_time_us)("btime", max_block_cpu_usage)
                    );
                    dgp.configuration.max_block_cpu_usage = override_time_us;
                    // pwnd_options.max_block_cpu_usage = override_time_us;
                }
                should_perform_override = false;
                ilog("pwnd global_property_object!");
                // uint64_t CPU_TARGET = EOS_PERCENT(override_time_us, gpo.configuration.target_block_cpu_usage_pct);
                // auto& resource_limits = control->get_mutable_resource_limits_manager();
                // resource_limits.set_block_parameters(
                //     {
                //         CPU_TARGET,
                //         override_time_us,
                //         chain::config::block_cpu_usage_average_window_ms / chain::config::block_interval_ms,
                //         chain::config::maximum_elastic_resource_multiplier,
                //         {99, 100}, {1000, 999}
                //     },
                //     {
                //         EOS_PERCENT(gpo.configuration.max_block_net_usage, gpo.configuration.target_block_net_usage_pct),
                //         gpo.configuration.max_block_net_usage,
                //         chain::config::block_size_average_window_ms / chain::config::block_interval_ms,
                //         chain::config::maximum_elastic_resource_multiplier,
                //         {99, 100}, {1000, 999}
                //     }
                // );
                // ilog("updated block resource limits!");
                // prod_plug->update_runtime_options(pwnd_options);
                // ilog("updated producer_plugin runtime_options");
            });
        }
    };  // subst_plugin_impl

    subst_plugin::subst_plugin() :
        my(new subst_plugin_impl(app().get_io_service()))
    {}

    subst_plugin::~subst_plugin() {}

    void subst_plugin::set_program_options(options_description& cli, options_description& cfg) {
        auto options = cfg.add_options();
        options(
            "subst-by-name", bpo::value<vector<string>>()->composing(),
            "contract_name:new_contract.wasm. Whenever the contract deployed at \"contract_name\""
            "needs to run, substitute debug.wasm in "
            "its place and enable debugging support. This bypasses size limits, timer limits, and "
            "other constraints on debug.wasm. nodeos still enforces constraints on contract.wasm. "
            "(may specify multiple times)");
        options(
            "subst-manifest", bpo::value<string>()->default_value(std::string("")),
            "url. load susbtitution information from a remote json file.");
        options(
            "subst-manifest-interval", bpo::value<uint32_t>()->default_value(DEFAULT_MANIFEST_INTERVAL),
            "Time between manifest re-fetches");
        options(
            "subst-manifest-timeout", bpo::value<uint32_t>()->default_value(DEFAULT_MANIFEST_TIMEOUT),
            "Timeout for manifest http calls");
        options(
            "override-max-tx-time", bpo::value<uint32_t>(),
            "Override on chain max-transaction-time with value.");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        try {
            auto* _chain = app().find_plugin<chain_plugin>();

            my->init(_chain, options);

        } FC_LOG_AND_RETHROW()
    }

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {
        delete my->subst_ctx;
    }

    substitution_context& subst_plugin::context() {
        return *my->subst_ctx;
    }

}  // namespace eosio
