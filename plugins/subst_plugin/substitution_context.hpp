#pragma once


#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/multi_index_includes.hpp>

#include <eosio/chain_plugin/chain_plugin.hpp>

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
#include <eosio/chain/wasm_interface_private.hpp>
#include <eosio/chain/webassembly/eos-vm-oc/code_cache.hpp>
#endif


namespace eosio {

    using chain::name;
    using chain::digest_type;

    using chain::code_object;
    using chain::account_metadata_object;

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
    using chain::eosvmoc::code_descriptor;
    using chain::eosvmoc::code_cache_async;

    typedef chain::wasm_interface_impl::eosvmoc_tier eosvmoc_tier;
#endif


    struct by_account;
    class subst_meta_object : public chainbase::object<chain::subst_meta_object_type, subst_meta_object> {
        OBJECT_CTOR(subst_meta_object, (og_code)(s_code))

        id_type id;
        name account;                         // account name to subst
        uint64_t from_block;                  // enable subst from this block

        chain::shared_blob og_code;           // original code
        chain::shared_blob s_code;            // subst code

        const digest_type og_hash() const {
            if (og_code.size() == 0) return digest_type();
            return digest_type::hash(
                (const char *)og_code.data(), og_code.size());
        }

        const digest_type s_hash() const {
            return digest_type::hash(
                (const char *)s_code.data(), s_code.size());
        }
    };

    using subst_meta_index = chainbase::shared_multi_index_container<
        subst_meta_object,
        indexed_by<
            ordered_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(subst_meta_object, subst_meta_object::id_type, id)>,
            ordered_unique<tag<by_account>, BOOST_MULTI_INDEX_MEMBER(subst_meta_object, name, account)>
        >
    >;

    class substitution_context {
        public:
            substitution_context(
                chain::controller* _control
            ) : control(_control), db(&_control->mutable_db()) {}

            // nodeos code store getters
            const account_metadata_object* get_account_metadata_object(const name& account, bool check_result = true);
            const code_object*             get_codeobj(const name& account, bool check_result = true);
            const digest_type              get_codeobj_hash(const name& account);

            // upsert
            void upsert(const name& account, uint64_t from_block, const std::vector<uint8_t>& code);

            // perform substitution
            void activate(const name& account, bool save_og = true);

            // maybe swap back original contract
            void deactivate(const name& account);

            // remove substitution metadata for account
            void remove(const name& account);

            // given an account name return its metadata
            // throws if account doesn't exist
            const subst_meta_object* get_by_account(const name& account, bool check_result = true);

            // get set with all account names that have subst metadata on db
            const std::set<name> get_substitutions();

            // called before every action execution
            // performs the swaps when necesary
            void apply_hook(
                const digest_type& code_hash,
                uint8_t vm_type,
                uint8_t vm_version,
                eosio::chain::apply_context& context
            );

            void debug_print();

            void reset_caches(const name& account);

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
            std::optional<eosvmoc_tier>& get_eosvmoc() {
                return control->get_wasm_interface().my->eosvmoc;
            }
#endif

        private:
            chain::controller* control;
            chainbase::database* db;

            // register new substitution, starts with on chain contract info zeroed out
            // will get filled on first use
            void create(const name& account, uint64_t from_block, const std::vector<uint8_t>& s_code);

            // update existing substitution to use new code
            void update(const name& account, uint64_t from_block, const std::vector<uint8_t>& code);
    };
}

CHAINBASE_SET_INDEX_TYPE(eosio::subst_meta_object, eosio::subst_meta_index)

FC_REFLECT(eosio::subst_meta_object, (account)(from_block)(og_code)(s_code))
