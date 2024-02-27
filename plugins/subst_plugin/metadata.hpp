#pragma once

#include <eosio/chain/multi_index_includes.hpp>

namespace eosio {
    struct by_account;
    class subst_meta_object : public chainbase::object<chain::subst_meta_object_type, subst_meta_object> {
        OBJECT_CTOR(subst_meta_object, (og_code)(s_code))

        id_type id;
        name account;                  // account name to subst
        uint64_t from_block;           // enable subst from this block

        fc::sha256 og_hash;            // code_hash to subst
        chain::shared_blob og_code;    // original code

        fc::sha256 s_hash;             // subst hash, hash of new code
        chain::shared_blob s_code;     // subst code
    };

    using subst_meta_index = chainbase::shared_multi_index_container<
        subst_meta_object,
        indexed_by<
            ordered_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(subst_meta_object, subst_meta_object::id_type, id)>,
            ordered_unique<tag<by_account>, BOOST_MULTI_INDEX_MEMBER(subst_meta_object, name, account)>
        >
    >;
}

CHAINBASE_SET_INDEX_TYPE(eosio::subst_meta_object, eosio::subst_meta_index)

FC_REFLECT(eosio::subst_meta_object, (account)(og_hash)(og_code)(from_block)(s_hash)(s_code))
