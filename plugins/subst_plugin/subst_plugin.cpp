#include "subst_plugin.hpp"

namespace eosio {

    static auto _subst_plugin = application::register_plugin<subst_plugin>();

    struct subst_plugin_impl : std::enable_shared_from_this<subst_plugin_impl> {

        chainbase::database* db;
        controller* control;

        fc::http_client httpc;
        appbase::variables_map app_options;

        bool override_tx_time = false;
        bool should_perform_override = false;
        uint32_t override_time = 300;

        void subst_meta_new(
            name account,
            uint64_t from_block,
            std::vector<uint8_t> s_code
        ) {
            auto s_hash = fc::sha256::hash((const char*)s_code.data(), s_code.size());

            db->create<subst_meta_object>([&](subst_meta_object& meta) {
                meta.account = account;
                meta.from_block = from_block;

                meta.og_hash = ZERO_SHA;

                meta.s_hash = s_hash;
                meta.s_code.assign(s_code.data(), s_code.size());
            });

            ilog("created new subst metadata row for ${acc}", ("acc", account));
        }

        const subst_meta_object* subst_meta_get_by_account(name account) {
            return db->find<subst_meta_object, by_account>(account);
        }

        void subst_meta_update(
            name account,
            uint64_t from_block,
            std::vector<uint8_t> code
        ) {
            const auto& meta_itr = subst_meta_get_by_account(account);

            // subst_meta_object for account must exist
            EOS_ASSERT(
                meta_itr,
                fc::assert_exception,
                "Substitution metadata for account ${acc} not found!",
                ("acc", account)
            );

            auto hash = fc::sha256::hash((const char*)code.data(), code.size());

            db->modify(*meta_itr, [&](subst_meta_object& meta) {
                meta.s_code.assign(code.data(), code.size());
                meta.s_hash = hash;
            });

            ilog("updated subst metadata row for ${acc}", ("acc", account));
        }

        void subst_meta_register_subst_or_update(
            std::string subst_info,
            std::vector<uint8_t> code
        ) {
            std::vector<std::string> v;
            boost::split(v, subst_info, boost::is_any_of("-"));

            name account;
            auto from_block = 0;

            if (v.size() == 2) {
                account = name(v[0]);
                from_block = std::stoul(v[1]);

            } else
                account = name(subst_info);

            ilog("registering subst for ${acc}", ("acc",account));

            auto meta = db->find<subst_meta_object, by_account>(account);

            if(meta)
                subst_meta_update(account, from_block, code);

            else
                subst_meta_new(account, from_block, code);
        }

        void subst_meta_swap_on_chain(
            const subst_meta_object* meta_itr,
            uint8_t vm_type, uint8_t vm_version
        ) {
            EOS_ASSERT(
                meta_itr,
                fc::assert_exception,
                "Tried to swap code on an inexistant subst_meta_id ${sid}",
                ("sid", meta_itr->id)
            );

            const chain::code_object* on_chain_co = db->find<chain::code_object, chain::by_code_hash>(
                boost::make_tuple(meta_itr->og_hash, vm_type, vm_version));

            EOS_ASSERT(
                meta_itr,
                fc::assert_exception,
                "Tried to swap code on an inexistant cbo ${ohash}",
                ("ohash", meta_itr->og_hash)
            );

            auto code = on_chain_co->code;

            auto hash = fc::sha256::hash((const char*)code.data(), code.size());

            ilog("test");

            db->modify(*meta_itr, [&](subst_meta_object& meta) {
                meta.og_code.assign(code.data(), code.size());
                meta.og_hash = hash;
            });

            ilog("test1");

            db->modify(*on_chain_co, [&](chain::code_object& o) {
                o.code.assign(meta_itr->s_code.data(), meta_itr->s_code.size());
                o.vm_type = 0;
                o.vm_version = 0;
            });

            ilog(
                "performed swap for account ${acc} \"${ohash}\" -> \"${shash}\"",
                ("acc", meta_itr->account)("ohash", hash)("shash", meta_itr->s_hash)
            );
        }

        void subst_meta_debug_print() {
            const auto& meta_idx = db->get_index<subst_meta_index, by_id>();
            ilog("substitution metadata on db: ");
            for (auto itr = meta_idx.begin(); itr != meta_idx.end(); itr++) {
                ilog(
                    "${id}: account \"${acc}\" from block ${fblock} "
                    "on-chain hash: \"${ohash}\" -> subst hash \"${shash}\"",
                    ("id", itr->id)("acc", itr->account)("fblock", itr->from_block)("ohash", itr->og_hash)("shash", itr->s_hash)
                );
            }
        }

        void subst_meta_maybe_update(
            fc::sha256 code_hash,
            eosio::chain::apply_context& context,
            uint8_t vm_type,
            uint8_t vm_version
        ) {
            eosio::name receiver = context.get_receiver();
            auto act = context.get_action();
            auto block_num = context.control.pending_block_num();


            const auto& meta = subst_meta_get_by_account(receiver);

            if (!meta)
                return;

            if (block_num >= meta->from_block) {

                if (meta->og_hash == ZERO_SHA ||
                    meta->og_hash != code_hash) {
                    ilog("must swap ${acc}", ("acc", meta->account));
                    subst_meta_swap_on_chain(meta, vm_type, vm_version);
                }
            }

        }

        void init(chain_plugin* chain, const variables_map& options) {
            app_options = options;

            control = &chain->chain();
            db = &control->mutable_db();

            control->get_wasm_interface().substitute_apply = [&](
                const eosio::chain::digest_type& code_hash,
                uint8_t vm_type, uint8_t vm_version,
                eosio::chain::apply_context& context
            ) {
                return substitute_apply(code_hash, vm_type, vm_version, context);
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

            std::string chain_id = control->get_chain_id();

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
                    subst_meta_register_subst_or_update(sinfo, new_code);
                }
            }
            if (app_options.count("subst-manifest")) {
                auto substs = app_options.at("subst-manifest").as<vector<string>>();
                for (auto& s : substs) {
                    auto manifest_url = fc::url(s);
                    EOS_ASSERT(
                        manifest_url.proto() == "http",
                        fc::invalid_arg_exception,
                        "Only http protocol supported for now."
                    );
                    load_remote_manifest(chain_id, manifest_url);
                }
            }

            subst_meta_debug_print();
        }

        bool substitute_apply(
            const fc::sha256& code_hash,
            uint8_t vm_type,
            uint8_t vm_version,
            eosio::chain::apply_context& context
        ) {
            try {
                eosio::name receiver = context.get_receiver();
                auto act = context.get_action();

                if (override_tx_time) {
                    if (should_perform_override) pwn_gpo();

                    if (receiver == eosio::name("eosio") &&
                        act.name == eosio::name("setparams")) {

                        should_perform_override = true;
                        ilog(
                            "setparams detected at ${bnum}, pwning gpo on next action",
                            ("bnum", control->pending_block_num())
                        );
                    }
                }

                subst_meta_maybe_update(code_hash, context, vm_type, vm_version);

                return false;
            } FC_LOG_AND_RETHROW()
        }

        void load_remote_manifest(std::string chain_id, fc::url manifest_url) {
            string upath = manifest_url.path()->generic_string();

            if (!boost::algorithm::ends_with(upath, "subst.json"))
                wlog("looks like provided url based substitution manifest"
                        "doesn\'t end with \"susbt.json\"... trying anyways...");

            fc::variant manifest = httpc.get_sync_json(manifest_url);
            auto& manif_obj = manifest.get_object();

            ilog("got manifest from ${url}", ("url", manifest_url));

            auto it = manif_obj.find(chain_id);
            if (it != manif_obj.end()) {
                for (auto subst_entry : (*it).value().get_object()) {
                    bpath url_path = *(manifest_url.path());
                    auto wasm_url_path = url_path.remove_filename() / chain_id / subst_entry.value().get_string();

                    auto wasm_url = fc::url(
                        manifest_url.proto(), manifest_url.host(), manifest_url.user(), manifest_url.pass(),
                        wasm_url_path,
                        manifest_url.query(), manifest_url.args(), manifest_url.port()
                    );

                    ilog("downloading wasm from ${wurl}...", ("wurl", wasm_url));
                    std::vector<uint8_t> new_code = httpc.get_sync_raw(wasm_url);
                    ilog("done.");

                    std::string subst_info = subst_entry.key();
                    subst_meta_register_subst_or_update(subst_info, new_code);
                }
            } else {
                ilog("manifest found but chain id not present.");
            }
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
        my(std::make_shared<subst_plugin_impl>())
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
            "subst-manifest", bpo::value<vector<string>>()->composing(),
            "url. load susbtitution information from a remote json file.");
        options(
            "override-max-tx-time", bpo::value<uint32_t>(),
            "Override on chain max-transaction-time with value.");
    }

    void subst_plugin::plugin_initialize(const variables_map& options) {
        try {
            auto* chain_plug = app().find_plugin<chain_plugin>();

            my->init(chain_plug, options);
        } FC_LOG_AND_RETHROW()
    }  // subst_plugin::plugin_initialize

    void subst_plugin::plugin_startup() {}

    void subst_plugin::plugin_shutdown() {}

}  // namespace eosio
