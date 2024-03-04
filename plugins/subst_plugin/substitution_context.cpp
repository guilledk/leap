#include "substitution_context.hpp"

namespace eosio {

    const subst_meta_object* substitution_context::get_by_account(const name& account, bool check_result) {
        const subst_meta_object* meta_ref = db->find<subst_meta_object, by_account>(account);

        if (check_result) {
            EOS_ASSERT(
                meta_ref,
                fc::assert_exception,
                "substitution metadata for account ${acc} not found!",
                ("acc", account)
            );
        }

        return meta_ref;
    }

    const account_metadata_object* substitution_context::get_account_metadata_object(const name& account, bool check_result) {
        const account_metadata_object* acc_meta = db->find<account_metadata_object, chain::by_name>(account);

        if (check_result) {
            EOS_ASSERT(
                acc_meta,
                fc::assert_exception,
                "account_metadata_object not found for ${acc}!",
                ("acc", account)
            );
        }

        return acc_meta;
    }


    const chain::code_object* substitution_context::get_codeobj(const name& account, bool check_result) {

        const auto& acc_meta = get_account_metadata_object(account);

        const chain::code_object* cobj = db->find<chain::code_object, chain::by_code_hash>(
            boost::make_tuple(acc_meta->code_hash, acc_meta->vm_type, acc_meta->vm_version));

        if (check_result) {
            EOS_ASSERT(
                cobj,
                fc::assert_exception,
                "code object for account ${acc}: (${hash},${vmt},${vmv}) not found!",
                ("acc", account)
                ("hash", acc_meta->code_hash)("vmt", acc_meta->vm_type)("vmv", acc_meta->vm_version)
            );
        }

        return cobj;
    }


    const digest_type substitution_context::get_codeobj_hash(const name& account) {
        const auto& cobj = get_codeobj(account);
        return digest_type::hash((const char*)cobj->code.data(), cobj->code.size());
    }


    void substitution_context::create(
        const name& account,
        uint64_t from_block,
        const std::vector<uint8_t>& s_code
    ) {
        const auto& meta = db->create<subst_meta_object>([&](subst_meta_object& meta) {
            meta.account = account;
            meta.from_block = from_block;

            meta.s_code.assign(s_code.data(), s_code.size());
        });
        ilog(
            "created new substitution metadata entry for ${acc} shash: ${shash}",
            ("acc", account)("shash", meta.s_hash())
        );
    }


    void substitution_context::update(
        const name& account,
        uint64_t from_block,
        const std::vector<uint8_t>& code
    ) {
        const auto& meta_itr = get_by_account(account);

        auto hash = digest_type::hash((const char*)code.data(), code.size());

        db->modify(*meta_itr, [&](subst_meta_object& meta) {
            meta.s_code.assign(code.data(), code.size());
        });

        ilog(
            "updated substitution metadata for ${acc} from block ${fblock} use ${hash}, current actual ${ahash}",
            ("acc", account)("fblock", from_block)("hash", hash)("ahash", get_codeobj_hash(account))
        );
    }

    void substitution_context::upsert(
        const name& account,
        uint64_t from_block,
        const std::vector<uint8_t>& code
    ) {
        if(get_by_account(account, false))
            update(account, from_block, code);

        else
            create(account, from_block, code);
    }


    void substitution_context::activate(
        const name& account,
        bool save_og
    ) {
        const auto& meta = get_by_account(account);
        const auto& cobj = get_codeobj(account);

        if (save_og) {
            auto code = cobj->code;
            db->modify(*meta, [&](subst_meta_object& m) {
                m.og_code.assign(code.data(), code.size());
            });
        }

        reset_caches(account);

        db->modify(*cobj, [&](chain::code_object& o) {
            o.code.assign(meta->s_code.data(), meta->s_code.size());
            o.vm_type = 0;
            o.vm_version = 0;
        });

        ilog(
            "swapped ${acc}: ${hash} for ${shash}, actual: ${ahash}",
            ("acc", account)
            ("hash", meta->og_hash())
            ("shash", meta->s_hash())
            ("ahash", get_codeobj_hash(account))
        );
    }


    void substitution_context::reset_caches(const name& account) {
        // remove wasm module cache if present
        control->get_wasm_interface().current_lib(
            control->pending_block_num());

        std::string msg = "did reset of wasm_iface";

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
        const auto& cobj = get_codeobj(account);

        // remove eosvmoc code cache
        std::optional<eosvmoc_tier>& eosvmoc = get_eosvmoc();

        if (eosvmoc) {
            eosvmoc->cc.free_code(cobj->code_hash, cobj->vm_version);
            msg += " & eosvmoc";
        }
#endif

        ilog(msg + " code cache");
    }


    const std::set<name> substitution_context::get_substitutions() {
        std::set<name> subs;
        const auto& meta_idx = db->get_index<subst_meta_index, by_account>();
        for (auto itr = meta_idx.begin(); itr != meta_idx.end(); itr++)
            subs.insert(itr->account);

        return subs;
    }


    void substitution_context::deactivate(const name& account) {
        const auto& meta = get_by_account(account);
        const auto& cobj = get_codeobj(account);

        const digest_type& cobj_hash = get_codeobj_hash(account);

        if (cobj_hash == meta->s_hash()) {
            db->modify(*cobj, [&](chain::code_object& o) {
                o.code.assign(meta->og_code.data(), meta->og_code.size());
                o.vm_type = 0;
                o.vm_version = 0;
            });
            ilog(
                "deactivated subst ${acc}, had ${hash} and set it back to ${ohash}",
                ("acc", account)
                ("hash", cobj_hash)
                ("ohash", meta->og_hash())
            );

            reset_caches(account);

        } else
            ilog("no need to deactivate ${acc}, subst not applied", ("acc", account));
    }


    void substitution_context::remove(const name& account) {
        const subst_meta_object* acc = get_by_account(account);
        db->remove(*acc);
        ilog("removed substitution metadata for ${acc}", ("acc", account));
    }


    void substitution_context::debug_print() {
        ilog("substitution metadata on db: ");
        for (const name& acc : get_substitutions()) {
            const auto& meta = get_by_account(acc);
            ilog(
                "${id}: account \"${acc}\" from block ${fblock} "
                "on-chain hash: ${ohash} -> subst hash ${shash}",
                ("id", meta->id)
                ("acc", meta->account)
                ("fblock", meta->from_block)
                ("ohash", meta->og_hash())("shash", meta->s_hash())
            );
        }
    }


    void substitution_context::apply_hook(
        const digest_type& code_hash,
        uint8_t vm_type,
        uint8_t vm_version,
        eosio::chain::apply_context& context
    ) {
        const name& receiver = context.get_receiver();
        auto act = context.get_action();
        uint32_t block_num = context.control.pending_block_num();

        const auto& meta = get_by_account(receiver);

        if (!meta)
            return;  // no subst for this contract

        if (block_num >= meta->from_block) {  // if we are in subst range

            if (code_hash != meta->og_hash()) {  // on chain code changed for this account, need to store copy and swap
                ilog(
                    "action ${recv}::${aname}, must swap ${acc} cause code_hash (${chash}) != meta og hash(${ohash})",
                    ("recv", receiver)("aname", act.name)
                    ("acc", meta->account)
                    ("chash", code_hash)("ohash", meta->og_hash())
                );

                // perform swap
                activate(meta->account);

            } else if (get_codeobj_hash(meta->account) != meta->s_hash()) {  // new code for subst detected, re apply subst
                ilog(
                    "action ${recv}::${aname}, must swap ${acc} cause cobj hash (${chash}) != meta s hash(${shash})",
                    ("recv", receiver)("aname", act.name)
                    ("acc", meta->account)
                    ("chash", get_codeobj_hash(meta->account))("shash", meta->s_hash())
                );

                // perform swap, but dont copy cobj code to meta
                activate(meta->account, false);
            }
        }
    }

}  // namespace eosio
