/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <graphene/app/api.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/wallet/contract_event_handler.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/rpc/api_connection.hpp>
using namespace graphene::app;
using namespace graphene::chain;
using namespace graphene::utilities;
using namespace std;

namespace fc
{
  void to_variant(const account_multi_index_type& accts, variant& vo);
  void from_variant(const variant &var, account_multi_index_type &vo);
}

#define CLI_WALLET_VERSION "1.3.5"

namespace graphene {
  namespace wallet {

    typedef uint16_t transaction_handle_type;

    /**
     * This class takes a variant and turns it into an object
     * of the given type, with the new operator.
     */

    object* create_object(const variant& v);

    struct crosschain_prkeys
    {
      string addr;
      string pubkey;
      string wif_key;
      bool operator==(const crosschain_prkeys& key) const;
    };
    struct brain_key_usage_info
    {
      string key;
      int next;
      map<string, int> used_indexes;
    };
    struct plain_keys
    {
      //map<public_key_type, string>  keys;
      map<address, string>  keys;
      map<string, crosschain_prkeys>    crosschain_keys;
      fc::sha512                    checksum;
    };

    struct brain_key_info
    {
      string brain_priv_key;
      string wif_priv_key;
      public_key_type pub_key;
    };


    /**
     *  Contains the confirmation receipt the sender must give the receiver and
     *  the meta data about the receipt that helps the sender identify which receipt is
     *  for the receiver and which is for the change address.
     */
    struct blind_confirmation
    {
      struct output
      {
        string                          label;
        public_key_type                 pub_key;
        stealth_confirmation::memo_data decrypted_memo;
        stealth_confirmation            confirmation;
        authority                       auth;
        string                          confirmation_receipt;
      };

      signed_transaction     trx;
      vector<output>         outputs;
    };

    struct blind_balance
    {
      asset                     amount;
      public_key_type           from; ///< the account this balance came from
      public_key_type           to; ///< the account this balance is logically associated with
      public_key_type           one_time_key; ///< used to derive the authority key and blinding factor
      fc::sha256                blinding_factor;
      fc::ecc::commitment_type  commitment;
      bool                      used = false;
    };

    struct blind_receipt
    {
      std::pair<public_key_type, fc::time_point>        from_date()const { return std::make_pair(from_key, date); }
      std::pair<public_key_type, fc::time_point>        to_date()const { return std::make_pair(to_key, date); }
      std::tuple<public_key_type, asset_id_type, bool>   to_asset_used()const { return std::make_tuple(to_key, amount.asset_id, used); }
      const commitment_type& commitment()const { return data.commitment; }

      fc::time_point                  date;
      public_key_type                 from_key;
      string                          from_label;
      public_key_type                 to_key;
      string                          to_label;
      asset                           amount;
      string                          memo;
      authority                       control_authority;
      stealth_confirmation::memo_data data;
      bool                            used = false;
      stealth_confirmation            conf;
    };

    struct by_from;
    struct by_to;
    struct by_to_asset_used;
    struct by_commitment;

    typedef multi_index_container< blind_receipt,
      indexed_by<
      ordered_unique< tag<by_commitment>, const_mem_fun< blind_receipt, const commitment_type&, &blind_receipt::commitment > >,
      ordered_unique< tag<by_to>, const_mem_fun< blind_receipt, std::pair<public_key_type, fc::time_point>, &blind_receipt::to_date > >,
      ordered_non_unique< tag<by_to_asset_used>, const_mem_fun< blind_receipt, std::tuple<public_key_type, asset_id_type, bool>, &blind_receipt::to_asset_used > >,
      ordered_unique< tag<by_from>, const_mem_fun< blind_receipt, std::pair<public_key_type, fc::time_point>, &blind_receipt::from_date > >
      >
    > blind_receipt_index_type;


    struct key_label
    {
      string          label;
      public_key_type key;
    };


    struct by_label;
    struct by_key;
    typedef multi_index_container<
      key_label,
      indexed_by<
      ordered_unique< tag<by_label>, member< key_label, string, &key_label::label > >,
      ordered_unique< tag<by_key>, member< key_label, public_key_type, &key_label::key > >
      >
    > key_label_index_type;


    struct wallet_data
    {
      /** Chain ID this wallet is used with */
      chain_id_type chain_id;
      account_multi_index_type my_accounts;

      script_object_multi_index_type my_scripts;

      script_binding_object_multi_index_type event_handlers;
      // script bindings
      fc::mutex script_lock;
      //
      vector<string> mining_accounts;
      vector<signed_transaction> pending_transactions;
      /// @return IDs of all accounts in @ref my_accounts
      vector<object_id_type> my_account_ids()const
      {
        vector<object_id_type> ids;
        ids.reserve(my_accounts.size());
        std::transform(my_accounts.begin(), my_accounts.end(), std::back_inserter(ids),
          [](const account_object& ao) { return ao.id; });
        return ids;
      }
      /// Add acct to @ref my_accounts, or update it if it is already in @ref my_accounts
      /// @return true if the account was newly inserted; false if it was only updated
      bool update_account(const account_object& acct)
      {
        auto& idx = my_accounts.get<by_address>();
        auto itr = idx.find(acct.addr);
        if (itr != idx.end())
        {
          idx.replace(itr, acct);
          return false;
        }
        else {
          idx.insert(acct);
          return true;
        }
      }
      void updata_account_name(const account_object& acct, const string& old_name) {
        auto& idx = my_accounts.get<by_name>();
        auto itr = idx.find(old_name);
        FC_ASSERT((itr != idx.end()), "account couldnt found");
        idx.replace(itr, acct);
      }
      vector<script_object> list_scripts()
      {
        fc::scoped_lock<fc::mutex> lock(script_lock);
        vector<script_object> res;
        auto& idx = my_scripts.get<by_hash>();
        for (auto itr : idx)
        {
          res.push_back(itr);
        }
        return res;
      }
      optional<script_object> get_script_by_hash(const string& contract_hash)
      {

        fc::scoped_lock<fc::mutex> lock(script_lock);
        vector<script_object> res;
        auto& idx = my_scripts.get<by_hash>();
        auto it = idx.find(contract_hash);
        if (it == idx.end())
          return optional<script_object>();
        return *it;
      }
      bool insert_script(script_object& spt)
      {

        fc::scoped_lock<fc::mutex> lock(script_lock);
        auto& id_idx = my_scripts.get<by_id>();
        uint64_t new_id = 0;
        if (id_idx.rbegin() != id_idx.rend())
          new_id = id_idx.rbegin()->id.instance() + 1;
        auto& idx = my_scripts.get<by_hash>();
        auto itr = idx.find(spt.script_hash);
        if (itr != idx.end())
        {
          spt.id.number = itr->id.number;
          idx.replace(itr, spt);
          return false;
        }
        else {

          spt.id.number = object_id_type(spt.space_id, spt.type_id, new_id).number;
          idx.insert(spt);
          return true;
        }
      }
      bool remove_script(const string& strhash)
      {

        fc::scoped_lock<fc::mutex> lock(script_lock);
        auto& idx = my_scripts.get<by_hash>();
        auto itr = idx.find(strhash);
        if (itr != idx.end())
        {
          idx.erase(itr);
          return true;
        }
        else {
          return false;
        }
      }
      bool bind_script_to_event(const string& script_hash, const chain::address& contract, const string& event_name)
      {

        fc::scoped_lock<fc::mutex> lock(script_lock);
        auto& id_idx = my_scripts.get<by_id>();
        uint64_t new_id = 0;
        if (id_idx.rbegin() != id_idx.rend())
          new_id = id_idx.rbegin()->id.instance() + 1;
        auto& idx = event_handlers.get<by_contract_addr>();
        auto itr = idx.lower_bound(contract);
        auto itr_end = idx.upper_bound(contract);

        while (itr != itr_end)
        {
          if (itr->script_hash == script_hash && itr->event_name == event_name)
            return false;
          itr++;
        }
        script_binding_object obj;
        obj.id.number = object_id_type(obj.space_id, obj.type_id, new_id).number;
        obj.script_hash = script_hash;
        obj.contract_id = contract;
        obj.event_name = event_name;
        idx.insert(obj);
        return true;
      }
      bool remove_event_handle(const string& script_hash, const chain::address& contract, const string& event_name)
      {

        fc::scoped_lock<fc::mutex> lock(script_lock);
        auto& idx = event_handlers.get<by_contract_addr>();
        auto itr = idx.lower_bound(contract);
        auto itr_end = idx.upper_bound(contract);
        while (itr != itr_end)
        {
          if (itr->script_hash == script_hash && itr->event_name == event_name)
          {
            idx.erase(itr);
            return true;
          }
          itr++;
        }
        return false;
      }
      bool update_handler(const object_id_type& id, const vector<std::pair<object_id_type, chain::contract_event_notify_object>>& modifies, bool add)
      {
        auto& idx = event_handlers.get<by_id>();
        auto it = idx.find(id);
        if (it == idx.end())
          return false;

        auto new_item = *it;
        for (auto& item : modifies)
        {
          if (add)
            new_item.handled.insert(std::make_pair(item.first, item.second));
          else
          {
            new_item.handled.erase(item.first);
          }

        }

        idx.replace(it, new_item);
        return true;
      }
      /** encrypted keys */
      vector<char>              cipher_keys;
      optional<vector<char>>              cipher_keys_extend;


      /** map an account to a set of extra keys that have been imported for that account */
      map<account_id_type, set<public_key_type> >  extra_keys;

      // map of account_name -> base58_private_key for
      //    incomplete account regs
      map<string, address > pending_account_registrations;
      map<transaction_id_type, string>pending_account_updation;
      map<string, string> pending_miner_registrations;
      map<address, transaction_id_type> pending_name_transfer;
      key_label_index_type                                              labeled_keys;
      blind_receipt_index_type                                          blind_receipts;

      string                    ws_server = "ws://localhost:8090";
      string                    ws_user;
      string                    ws_password;
    };

    struct exported_account_keys
    {
      string account_name;
      vector<vector<char>> encrypted_private_keys;
      vector<public_key_type> public_keys;
    };

    struct exported_keys
    {
      fc::sha512 password_checksum;
      vector<exported_account_keys> account_keys;
    };

    struct approval_delta
    {
      vector<string> active_approvals_to_add;
      vector<string> active_approvals_to_remove;
      vector<string> owner_approvals_to_add;
      vector<string> owner_approvals_to_remove;
      vector<string> key_approvals_to_add;
      vector<string> key_approvals_to_remove;
    };

    struct worker_vote_delta
    {
      flat_set<worker_id_type> vote_for;
      flat_set<worker_id_type> vote_against;
      flat_set<worker_id_type> vote_abstain;
    };

    struct signed_block_with_info : public signed_block
    {
      signed_block_with_info(const signed_block& block);
      signed_block_with_info(const signed_block_with_info& block) = default;

      uint32_t      number;
      block_id_type block_id;
      public_key_type signing_key;
      share_type     reward;
      vector< transaction_id_type > transaction_ids;
    };


    namespace detail {
      class wallet_api_impl;
    }

    /***
     * A utility class for performing various state-less actions that are related to wallets
     */
    class utility {
    public:
      /**
       * Derive any number of *possible* owner keys from a given brain key.
       *
       * NOTE: These keys may or may not match with the owner keys of any account.
       * This function is merely intended to assist with account or key recovery.
       *
       * @see suggest_brain_key()
       *
       * @param brain_key    Brain key
       * @param number_of_desired_keys  Number of desired keys
       * @return A list of keys that are deterministically derived from the brainkey
       */
      static vector<brain_key_info> derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys = 1);
    };

    struct operation_detail {
      string                   memo;
      string                   description;
      operation_history_object op;
    };

    /**
     * This wallet assumes it is connected to the database server with a high-bandwidth, low-latency connection and
     * performs minimal caching. This API could be provided locally to be used by a web interface.
     */
    class wallet_api
    {
    public:
      wallet_api(const wallet_data& initial_data, fc::api<login_api> rapi);
      virtual ~wallet_api();

      bool copy_wallet_file(string destination_filename);

      fc::ecc::private_key derive_private_key(const std::string& prefix_string, int sequence_number) const;

      void change_acquire_plugin_num(const string&symbol, const uint32_t& blocknum);

      variant                           info();
      /** Returns info such as client version, git version of graphene/fc, version of boost, openssl.
       * @returns compile time info and client and dependencies versions
       */
      variant_object                    about() const;
      optional<signed_block_with_info>    get_block(uint32_t num);
      /** Returns the number of accounts registered on the blockchain
       * @returns the number of registered accounts
       */
      uint64_t                          get_account_count()const;
      /** Lists all accounts controlled by this wallet.
       * This returns a list of the full account objects for all accounts whose private keys
       * we possess.
       * @returns a list of account objects
       */
      vector<account_object>            list_my_accounts();
      /** Lists all accounts registered in the blockchain.
       * This returns a list of all account names and their account ids, sorted by account name.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all accounts,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last account name returned as the \c lowerbound for the next \c list_accounts() call.
       *
       * @param lowerbound the name of the first account to return.  If the named account does not exist,
       *                   the list will start at the account that comes after \c lowerbound
       * @param limit the maximum number of accounts to return (max: 1000)
       * @returns a list of accounts mapping account names to account ids
       */
      map<string, account_id_type>       list_accounts(const string& lowerbound, uint32_t limit);
      /** List the balances of an account.
       * Each account can have multiple balances, one for each type of asset owned by that
       * account.  The returned list will only contain assets for which the account has a
       * nonzero balance
       * @param id the name or id of the account whose balances you want
       * @returns a list of the given account's balances
       */
      vector<asset>                     list_account_balances(const string& id);
      /** List the balances of an account.
      * Each account can have multiple balances, one for each type of asset owned by that
      * account.  The returned list will only contain assets for which the account has a
      * nonzero balance
      * @param id the name or id of the account whose balances you want
      * @returns a list of the given account's balances
      */
      vector<asset>                     get_addr_balances(const string& addr);

      /** List the balances of an account.
      * Each account can have multiple balances, one for each type of asset owned by that
      * account.  The returned list will only contain assets for which the account has a
      * nonzero balance
      * @param id the name or id of the account whose balances you want
      * @returns a list of the given account's balances
      */
      vector<asset>                     get_account_balances(const string& account);

      /** Lists all assets registered on the blockchain.
       *
       * To list all assets, pass the empty string \c "" for the lowerbound to start
       * at the beginning of the list, and iterate as necessary.
       *
       * @param lowerbound  the symbol of the first asset to include in the list.
       * @param limit the maximum number of assets to return (max: 100)
       * @returns the list of asset objects, ordered by symbol
       */
      vector<asset_object>              list_assets(const string& lowerbound, uint32_t limit)const;

      /** Returns the most recent operations on the named account.
       *
       * This returns a list of operation history objects, which describe activity on the account.
       *
       * @param name the name or id of the account
       * @param limit the number of entries to return (starting from the most recent)
       * @returns a list of \c operation_history_objects
       */
      vector<operation_detail>  get_account_history(string name, int limit)const;

      /** Returns the relative operations on the named account from start number.
       *
       * @param name the name or id of the account
       * @param stop Sequence number of earliest operation.
       * @param limit the number of entries to return
       * @param start  the sequence number where to start looping back throw the history
       * @returns a list of \c operation_history_objects
       */
      vector<operation_detail>  get_relative_account_history(string name, uint32_t stop, int limit, uint32_t start)const;

      vector<bucket_object>             get_market_history(string symbol, string symbol2, uint32_t bucket, fc::time_point_sec start, fc::time_point_sec end)const;
      vector<limit_order_object>        get_limit_orders(string a, string b, uint32_t limit)const;
      vector<call_order_object>         get_call_orders(string a, uint32_t limit)const;
      vector<force_settlement_object>   get_settle_orders(string a, uint32_t limit)const;

      /** Returns the block chain's slowly-changing settings.
       * This object contains all of the properties of the blockchain that are fixed
       * or that change only once per maintenance interval (daily) such as the
       * current list of witnesses, committee_members, block interval, etc.
       * @see \c get_dynamic_global_properties() for frequently changing properties
       * @returns the global properties
       */
      global_property_object            get_global_properties() const;

      /** Returns the block chain's rapidly-changing properties.
       * The returned object contains information that changes every block interval
       * such as the head block number, the next witness, etc.
       * @see \c get_global_properties() for less-frequently changing properties
       * @returns the dynamic global properties
       */
      dynamic_global_property_object    get_dynamic_global_properties() const;

      /** Returns information about the given account.
       *
       * @param account_name_or_id the name or id of the account to provide information about
       * @returns the public account data stored in the blockchain
       */
      fc::variant_object            get_account(string account_name_or_id) const;
      account_object change_account_name(const string& oldname, const string& newname);
      void remove_local_account(const string & account_name);

      /** Returns information about the given asset.
       * @param asset_name_or_id the symbol or id of the asset in question
       * @returns the information about the asset stored in the block chain
       */
      asset_object                      get_asset(string asset_name_or_id) const;
      fc::variant                      get_asset_imp(string asset_name_or_id) const;
      /** Returns the BitAsset-specific data for a given asset.
       * Market-issued assets's behavior are determined both by their "BitAsset Data" and
       * their basic asset data, as returned by \c get_asset().
       * @param asset_name_or_id the symbol or id of the BitAsset in question
       * @returns the BitAsset-specific data for this asset
       */
      asset_bitasset_data_object        get_bitasset_data(string asset_name_or_id)const;

      /** Lookup the id of a named account.
       * @param account_name_or_id the name of the account to look up
       * @returns the id of the named account
       */
      account_id_type                   get_account_id(string account_name_or_id) const;

      /** Lookup the id of a named account.
      * @param account_name_or_id the name of the account to look up
      * @returns the id of the named account
      */
      address                   get_account_addr(string account_name) const;


      /**
       * Lookup the id of a named asset.
       * @param asset_name_or_id the symbol of an asset to look up
       * @returns the id of the given asset
       */
      asset_id_type                     get_asset_id(string asset_name_or_id) const;

      /**
       * Returns the blockchain object corresponding to the given id.
       *
       * This generic function can be used to retrieve any object from the blockchain
       * that is assigned an ID.  Certain types of objects have specialized convenience
       * functions to return their objects -- e.g., assets have \c get_asset(), accounts
       * have \c get_account(), but this function will work for any object.
       *
       * @param id the id of the object to return
       * @returns the requested object
       */
      variant                           get_object(object_id_type id) const;

      /** Returns the current wallet filename.
       *
       * This is the filename that will be used when automatically saving the wallet.
       *
       * @see set_wallet_filename()
       * @return the wallet filename
       */
      string                            get_wallet_filename() const;

      /**
       * Get the WIF private key corresponding to a public key.  The
       * private key must already be in the wallet.
       */
      string                            get_private_key(public_key_type pubkey)const;

      /**
       * @ingroup Transaction Builder API
       */
      transaction_handle_type begin_builder_transaction();
      fc::variant build_transaction(fc::variant op);
      /**
       * @ingroup Transaction Builder API
       */
      void add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op);
      /**
       * @ingroup Transaction Builder API
       */
      void replace_operation_in_builder_transaction(transaction_handle_type handle,
        unsigned operation_index,
        const operation& new_op);
      /**
       * @ingroup Transaction Builder API
       */
      asset set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset = GRAPHENE_SYMBOL);
      /**
       * @ingroup Transaction Builder API
       */
      transaction preview_builder_transaction(transaction_handle_type handle);
      /**
       * @ingroup Transaction Builder API
       */
      full_transaction sign_builder_transaction(transaction_handle_type transaction_handle, bool broadcast = true);
      /**
       * @ingroup Transaction Builder API
       */
      full_transaction propose_builder_transaction(
        transaction_handle_type handle,
        time_point_sec expiration = time_point::now() + fc::minutes(1),
        uint32_t review_period_seconds = 0,
        bool broadcast = true
      );

      full_transaction propose_builder_transaction2(
        transaction_handle_type handle,
        string account_name_or_id,
        time_point_sec expiration = time_point::now() + fc::minutes(1),
        uint32_t review_period_seconds = 0,
        bool broadcast = true
      );
      std::map<std::string, asset> get_address_pay_back_balance(const address& owner_addr, std::string asset_symbol = "") const;
      std::map<std::string, share_type> get_bonus_balance(const address& owner_addr) const;
      full_transaction obtain_pay_back_balance(const string& pay_back_owner, std::map<std::string, asset> nums, bool broadcast = true);
      full_transaction obtain_bonus_balance(const string& bonus_owner, std::map<std::string, share_type> nums, bool broadcast = true);
      /**
       * @ingroup Transaction Builder API
       */
      void remove_builder_transaction(transaction_handle_type handle);

      /** Checks whether the wallet has just been created and has not yet had a password set.
       *
       * Calling \c set_password will transition the wallet to the locked state.
       * @return true if the wallet is new
       * @ingroup Wallet Management
       */
      bool    is_new()const;

      /** Checks whether the wallet is locked (is unable to use its private keys).
       *
       * This state can be changed by calling \c lock() or \c unlock().
       * @return true if the wallet is locked
       * @ingroup Wallet Management
       */
      bool    is_locked()const;

      /** Locks the wallet immediately.
       * @ingroup Wallet Management
       */
      void    lock();

      /** Unlocks the wallet.
       *
       * The wallet remain unlocked until the \c lock is called
       * or the program exits.
       * @param password the password previously set with \c set_password()
       * @ingroup Wallet Management
       */
      void    unlock(string password);

      /** Sets a new password on the wallet.
       *
       * The wallet must be either 'new' or 'unlocked' to
       * execute this command.
       * @ingroup Wallet Management
       */
      void    set_password(string password);

      /** Dumps all private keys owned by the wallet.
       *
       * The keys are printed in WIF format.  You can import these keys into another wallet
       * using \c import_key()
       * @returns a map containing the private keys, indexed by their public key
       */
      map<address, string> dump_private_keys();

      /** Dumps all private keys owned by the wallet.
      *
      * The keys are printed in WIF format.  You can import these keys into another wallet
      * using \c import_key()
      * @returns a map containing the private keys, indexed by their public key
      */
      map<address, string> dump_private_key(string account_name);
      map<string, crosschain_prkeys>  dump_crosschain_private_key(string pubkey);
      map<string, crosschain_prkeys>  dump_crosschain_private_keys();
      /** Returns a list of all commands supported by the wallet API.
       *
       * This lists each command, along with its arguments and return types.
       * For more detailed help on a single command, use \c get_help()
       *
       * @returns a multi-line string suitable for displaying on a terminal
       */
      string  help()const;

      /** Returns detailed help on a single API command.
       * @param method the name of the API command you want help with
       * @returns a multi-line string suitable for displaying on a terminal
       */
      string  gethelp(const string& method)const;

      /** Loads a specified Graphene wallet.
       *
       * The current wallet is closed before the new wallet is loaded.
       *
       * @warning This does not change the filename that will be used for future
       * wallet writes, so this may cause you to overwrite your original
       * wallet unless you also call \c set_wallet_filename()
       *
       * @param wallet_filename the filename of the wallet JSON file to load.
       *                        If \c wallet_filename is empty, it reloads the
       *                        existing wallet file
       * @returns true if the specified wallet is loaded
       */
      bool    load_wallet_file(string wallet_filename = "");

      /** Saves the current wallet to the given filename.
       *
       * @warning This does not change the wallet filename that will be used for future
       * writes, so think of this function as 'Save a Copy As...' instead of
       * 'Save As...'.  Use \c set_wallet_filename() to make the filename
       * persist.
       * @param wallet_filename the filename of the new wallet JSON file to create
       *                        or overwrite.  If \c wallet_filename is empty,
       *                        save to the current filename.
       */
      void    save_wallet_file(string wallet_filename = "");

      /** Sets the wallet filename used for future writes.
       *
       * This does not trigger a save, it only changes the default filename
       * that will be used the next time a save is triggered.
       *
       * @param wallet_filename the new filename to use for future saves
       */
      void    set_wallet_filename(string wallet_filename);

      /** Suggests a safe brain key to use for creating your account.
       * \c create_account_with_brain_key() requires you to specify a 'brain key',
       * a long passphrase that provides enough entropy to generate cyrptographic
       * keys.  This function will suggest a suitably random string that should
       * be easy to write down (and, with effort, memorize).
       * @returns a suggested brain_key
       */
      brain_key_info suggest_brain_key()const;

      /**
       * Derive any number of *possible* owner keys from a given brain key.
       *
       * NOTE: These keys may or may not match with the owner keys of any account.
       * This function is merely intended to assist with account or key recovery.
       *
       * @see suggest_brain_key()
       *
       * @param brain_key    Brain key
       * @param numberOfDesiredKeys  Number of desired keys
       * @return A list of keys that are deterministically derived from the brainkey
       */
      vector<brain_key_info> derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys = 1) const;

      /**
       * Determine whether a textual representation of a public key
       * (in Base-58 format) is *currently* linked
       * to any *registered* (i.e. non-stealth) account on the blockchain
       * @param public_key Public key
       * @return Whether a public key is known
       */
      bool is_public_key_registered(string public_key) const;

      /** Converts a signed_transaction in JSON form to its binary representation.
       *
       * TODO: I don't see a broadcast_transaction() function, do we need one?
       *
       * @param tx the transaction to serialize
       * @returns the binary form of the transaction.  It will not be hex encoded,
       *          this returns a raw string that may have null characters embedded
       *          in it
       */
      string serialize_transaction(signed_transaction tx) const;

      /** Imports the private key for an existing account.
       *
       * The private key must match either an owner key or an active key for the
       * named account.
       *
       * @see dump_private_keys()
       *
       * @param account_name_or_id the account owning the key
       * @param wif_key the private key in WIF format
       * @returns true if the key was imported
       */
      bool import_key(string account_name_or_id, string wif_key);
      bool import_crosschain_key(string wif_key, string symbol);
      map<string, bool> import_accounts(string filename, string password);

      bool import_account_keys(string filename, string password, string src_account_name, string dest_account_name);

      /**
       * This call will construct transaction(s) that will claim all balances controled
       * by wif_keys and deposit them into the given account.
       */
      vector< signed_transaction > import_balance(string account_name_or_id, const vector<string>& wif_keys, bool broadcast);

      /** Transforms a brain key to reduce the chance of errors when re-entering the key from memory.
       *
       * This takes a user-supplied brain key and normalizes it into the form used
       * for generating private keys.  In particular, this upper-cases all ASCII characters
       * and collapses multiple spaces into one.
       * @param s the brain key as supplied by the user
       * @returns the brain key in its normalized form
       */
      string normalize_brain_key(string s) const;

      /** Registers a third party's account on the blockckain.
       *
       * This function is used to register an account for which you do not own the private keys.
       * When acting as a registrar, an end user will generate their own private keys and send
       * you the public keys.  The registrar will use this function to register the account
       * on behalf of the end user.
       *
       * @see create_account_with_brain_key()
       *
       * @param name the name of the account, must be unique on the blockchain.  Shorter names
       *             are more expensive to register; the rules are still in flux, but in general
       *             names of more than 8 characters with at least one digit will be cheap.
       * @param owner the owner key for the new account
       * @param active the active key for the new account
       * @param registrar_account the account which will pay the fee to register the user
       * @param referrer_account the account who is acting as a referrer, and may receive a
       *                         portion of the user's transaction fees.  This can be the
       *                         same as the registrar_account if there is no referrer.
       * @param referrer_percent the percentage (0 - 100) of the new user's transaction fees
       *                         not claimed by the blockchain that will be distributed to the
       *                         referrer; the rest will be sent to the registrar.  Will be
       *                         multiplied by GRAPHENE_1_PERCENT when constructing the transaction.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering the account

      signed_transaction register_account(string name,
                                          public_key_type owner,
                                          public_key_type active,
                                          string  registrar_account,
                                          string  referrer_account,
                                          uint32_t referrer_percent,
                                          bool broadcast = false);
      */

      /** Registers a third party's account on the blockckain.
      *
      * This function is used to register an account for which you do not own the private keys.
      * When acting as a registrar, an end user will generate their own private keys and send
      * you the public keys.  The registrar will use this function to register the account
      * on behalf of the end user.
      *
      *
      * @param name the name of the account, must be unique on the blockchain.  Shorter names
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction registering the account
      */
      full_transaction register_account(string name, bool broadcast = true);


      /**
       *  Upgrades an account to prime status.
       *  This makes the account holder a 'lifetime member'.
       *
       *  @todo there is no option for annual membership
       *  @param name the name or id of the account to upgrade
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction upgrading the account
       */
      full_transaction upgrade_account(string name, bool broadcast);

      /** Creates a new account and registers it on the blockchain.
       *
       * @todo why no referrer_percent here?
       *
       * @see suggest_brain_key()
       * @see register_account()
       *
       * @param brain_key the brain key used for generating the account's private keys
       * @param account_name the name of the account, must be unique on the blockchain.  Shorter names
       *                     are more expensive to register; the rules are still in flux, but in general
       *                     names of more than 8 characters with at least one digit will be cheap.
       * @param registrar_account the account which will pay the fee to register the user
       * @param referrer_account the account who is acting as a referrer, and may receive a
       *                         portion of the user's transaction fees.  This can be the
       *                         same as the registrar_account if there is no referrer.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering the account
       */
       //full_transaction create_account_with_brain_key(string brain_key,
       //                                                 string account_name,
       //                                                 string registrar_account,
       //                                                 string referrer_account,
       //                                                 bool broadcast = false);



       /** Creates a new account and registers it on the blockchain.
       *
       * @todo why no referrer_percent here?
       *
       * @see suggest_brain_key()
       * @see register_account()
       *
       * @param brain_key the brain key used for generating the account's private keys
       * @param account_name the name of the account, must be unique on the blockchain.  Shorter names
       *                     are more expensive to register; the rules are still in flux, but in general
       *                     names of more than 8 characters with at least one digit will be cheap.
       * @param registrar_account the account which will pay the fee to register the user
       * @param referrer_account the account who is acting as a referrer, and may receive a
       *                         portion of the user's transaction fees.  This can be the
       *                         same as the registrar_account if there is no referrer.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering the account
       */
      address wallet_create_account(string account_name);

      /** Transfer an amount from one account to another.
       * @param from the name or id of the account sending the funds
       * @param to the name or id of the account receiving the funds
       * @param amount the amount to send (in nominal units -- to send half of a BIT, specify 0.5)
       * @param asset_symbol the symbol or id of the asset to send
       * @param memo a memo to attach to the transaction.  The memo will be encrypted in the
       *             transaction and readable for the receiver.  There is no length limit
       *             other than the limit imposed by maximum transaction size, but transaction
       *             increase with transaction size
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction transferring funds
       */
      full_transaction transfer(string from,
        string to,
        string amount,
        string asset_symbol,
        string memo,
        bool broadcast = false);

      /** Transfer an amount from one address to another.
      * @param from the name or id of the account sending the funds
      * @param to the name or id of the account receiving the funds
      * @param amount the amount to send (in nominal units -- to send half of a BIT, specify 0.5)
      * @param asset_symbol the symbol or id of the asset to send
      * @param memo a memo to attach to the transaction.  The memo will be encrypted in the
      *             transaction and readable for the receiver.  There is no length limit
      *             other than the limit imposed by maximum transaction size, but transaction
      *             increase with transaction size
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction transferring funds
      */
      full_transaction transfer_to_address(string from,
        string to,
        string amount,
        string asset_symbol,
        string memo,
        bool broadcast = false);
      /** this is only for the multisignature of XWC or
      * @param from the address from the account sending the funds
      * @param to the address to the account receiving the funds
      */
      string transfer_from_to_address(string from,
        string to,
        string amount,
        string asset_symbol,
        string memo);
      full_transaction combine_transaction(const vector<string>& trxs,
        bool broadcast = false
      );
      string name_transfer_to_address(string from, address to, asset amount, string newname);
      full_transaction confirm_name_transfer(string account, string trx, bool broadcast);
      string undertaker_customize(const string& maker, const address& taker, const fc::variant& maker_op, const fc::variant& taker_op);
      full_transaction confirm_undertaker(const string& taker, string trx, bool broadcast);
      /** broadcast a transaction to the chain.
     * @param trx  the transaction to broadcast
     * @returns the transaction id
     */
      string lightwallet_broadcast(signed_transaction trx);

      /**
       *  get referenced block info for light wallet
       *  @returns ref_block_num & ref_block_prefix
       */
      string lightwallet_get_refblock_info();


      /** Transfer an amount from one address to another.
      * @param from the name or id of the account sending the funds
      * @param to the name or id of the account receiving the funds
      * @param amount the amount to send (in nominal units -- to send half of a BIT, specify 0.5)
      * @param asset_symbol the symbol or id of the asset to send
      * @param memo a memo to attach to the transaction.  The memo will be encrypted in the
      *             transaction and readable for the receiver.  There is no length limit
      *             other than the limit imposed by maximum transaction size, but transaction
      *             increase with transaction size
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction transferring funds
      */
      full_transaction transfer_to_account(string from,
        string to,
        string amount,
        string asset_symbol,
        string memo,
        bool broadcast = false);

      full_transaction lock_balance_to_miner(string miner_account,
        string lock_account,
        string amount,
        string asset_symbol,
        bool broadcast = false);
      full_transaction lock_balance_to_miners(string lock_account,
        map<string, vector<asset>> lockbalances,
        bool broadcast = false);
      full_transaction wallfacer_lock_balance(string wallfacer_account,
        string amount,
        string asset_symbol,
        bool broadcast = false);
      full_transaction foreclose_balance_from_miner(string miner_account,
        string foreclose_account,
        string amount,
        string asset_symbol,
        bool broadcast = false);
      full_transaction foreclose_balance_from_miners(string foreclose_account,
        map<string, vector<asset>> foreclose_balances,
        bool broadcast = false);
      full_transaction wallfacer_foreclose_balance(string wallfacer_account,
        string amount,
        string asset_symbol,
        bool broadcast = false);
      full_transaction withdraw_cross_chain_transaction(string account_name,
        string amount,
        string asset_symbol,
        string crosschain_account,
        string memo,
        bool broadcast = false);
      full_transaction transfer_wallfacer_multi_account(string multi_account,
        string amount,
        string asset_symbol,
        string multi_to_account,
        string memo,
        bool broadcast = false);
      /**
       *  This method works just like transfer, except it always broadcasts and
       *  returns the transaction ID along with the signed transaction.
       */
      pair<transaction_id_type, signed_transaction> transfer2(string from,
        string to,
        string amount,
        string asset_symbol,
        string memo) {
        auto trx = transfer(from, to, amount, asset_symbol, memo, true);
        return std::make_pair(trx.id(), trx);
      }


      /**
       *  This method is used to convert a JSON transaction to its transactin ID.
       */
      transaction_id_type get_transaction_id(const signed_transaction& trx)const { return trx.id(); }


      /** These methods are used for stealth transfers */
      ///@{
      /**
       *  This method can be used to set the label for a public key
       *
       *  @note No two keys can have the same label.
       *
       *  @return true if the label was set, otherwise false
       */
      bool                        set_key_label(public_key_type, string label);
      string                      get_key_label(public_key_type)const;

      /**
       *  Generates a new blind account for the given brain key and assigns it the given label.
       */
      public_key_type             create_blind_account(string label, string brain_key);

      /**
       * @return the total balance of all blinded commitments that can be claimed by the
       * given account key or label
       */
      vector<asset>                get_blind_balances(string key_or_label);
      /** @return all blind accounts */
      map<string, public_key_type> get_blind_accounts()const;
      /** @return all blind accounts for which this wallet has the private key */
      map<string, public_key_type> get_my_blind_accounts()const;
      /** @return the public key associated with the given label */
      public_key_type             get_public_key(string label)const;
      ///@}

      /**
       * @return all blind receipts to/form a particular account
       */
      vector<blind_receipt> blind_history(string key_or_account);

      /**
       *  Given a confirmation receipt, this method will parse it for a blinded balance and confirm
       *  that it exists in the blockchain.  If it exists then it will report the amount received and
       *  who sent it.
       *
       *  @param opt_from - if not empty and the sender is a unknown public key, then the unknown public key will be given the label opt_from
       *  @param confirmation_receipt - a base58 encoded stealth confirmation
       */
      blind_receipt receive_blind_transfer(string confirmation_receipt, string opt_from, string opt_memo);

      /**
       *  Transfers a public balance from @from to one or more blinded balances using a
       *  stealth transfer.
       */
      blind_confirmation transfer_to_blind(string from_account_id_or_name,
        string asset_symbol,
        /** map from key or label to amount */
        vector<pair<string, string>> to_amounts,
        bool broadcast = false);

      /**
       * Transfers funds from a set of blinded balances to a public account balance.
       */
      blind_confirmation transfer_from_blind(
        string from_blind_account_key_or_label,
        string to_account_id_or_name,
        string amount,
        string asset_symbol,
        bool broadcast = false);

      /**
       *  Used to transfer from one set of blinded balances to another
       */
      blind_confirmation blind_transfer(string from_key_or_label,
        string to_key_or_label,
        string amount,
        string symbol,
        bool broadcast = false);

      /** Place a limit order attempting to sell one asset for another.
       *
       * Buying and selling are the same operation on Graphene; if you want to buy BIT
       * with USD, you should sell USD for BIT.
       *
       * The blockchain will attempt to sell the \c symbol_to_sell for as
       * much \c symbol_to_receive as possible, as long as the price is at
       * least \c min_to_receive / \c amount_to_sell.
       *
       * In addition to the transaction fees, market fees will apply as specified
       * by the issuer of both the selling asset and the receiving asset as
       * a percentage of the amount exchanged.
       *
       * If either the selling asset or the receiving asset is whitelist
       * restricted, the order will only be created if the seller is on
       * the whitelist of the restricted asset type.
       *
       * Market orders are matched in the order they are included
       * in the block chain.
       *
       * @todo Allow order expiration to be set here.  Document default/max expiration time
       *
       * @param seller_account the account providing the asset being sold, and which will
       *                       receive the proceeds of the sale.
       * @param amount_to_sell the amount of the asset being sold to sell (in nominal units)
       * @param symbol_to_sell the name or id of the asset to sell
       * @param min_to_receive the minimum amount you are willing to receive in return for
       *                       selling the entire amount_to_sell
       * @param symbol_to_receive the name or id of the asset you wish to receive
       * @param timeout_sec if the order does not fill immediately, this is the length of
       *                    time the order will remain on the order books before it is
       *                    cancelled and the un-spent funds are returned to the seller's
       *                    account
       * @param fill_or_kill if true, the order will only be included in the blockchain
       *                     if it is filled immediately; if false, an open order will be
       *                     left on the books to fill any amount that cannot be filled
       *                     immediately.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction selling the funds
       */
       /* full_transaction sell_asset(string seller_account,
                                      string amount_to_sell,
                                      string   symbol_to_sell,
                                      string min_to_receive,
                                      string   symbol_to_receive,
                                      uint32_t timeout_sec = 0,
                                      bool     fill_or_kill = false,
                                      bool     broadcast = false);
                                      */
                                      /** Place a limit order attempting to sell one asset for another.
                                       *
                                       * This API call abstracts away some of the details of the sell_asset call to be more
                                       * user friendly. All orders placed with sell never timeout and will not be killed if they
                                       * cannot be filled immediately. If you wish for one of these parameters to be different,
                                       * then sell_asset should be used instead.
                                       *
                                       * @param seller_account the account providing the asset being sold, and which will
                                       *                       receive the processed of the sale.
                                       * @param base The name or id of the asset to sell.
                                       * @param quote The name or id of the asset to recieve.
                                       * @param rate The rate in base:quote at which you want to sell.
                                       * @param amount The amount of base you want to sell.
                                       * @param broadcast true to broadcast the transaction on the network.
                                       * @returns The signed transaction selling the funds.
                                       */
      full_transaction sell(string seller_account,
        string base,
        string quote,
        double rate,
        double amount,
        bool broadcast);

      /** Place a limit order attempting to buy one asset with another.
       *
       * This API call abstracts away some of the details of the sell_asset call to be more
       * user friendly. All orders placed with buy never timeout and will not be killed if they
       * cannot be filled immediately. If you wish for one of these parameters to be different,
       * then sell_asset should be used instead.
       *
       * @param buyer_account The account buying the asset for another asset.
       * @param base The name or id of the asset to buy.
       * @param quote The name or id of the assest being offered as payment.
       * @param rate The rate in base:quote at which you want to buy.
       * @param amount the amount of base you want to buy.
       * @param broadcast true to broadcast the transaction on the network.
       * @param The signed transaction selling the funds.
       */
       /*full_transaction buy( string buyer_account,
                               string base,
                               string quote,
                               double rate,
                               double amount,
                               bool broadcast );
 */
 /** Borrow an asset or update the debt/collateral ratio for the loan.
  *
  * This is the first step in shorting an asset.  Call \c sell_asset() to complete the short.
  *
  * @param borrower_name the name or id of the account associated with the transaction.
  * @param amount_to_borrow the amount of the asset being borrowed.  Make this value
  *                         negative to pay back debt.
  * @param asset_symbol the symbol or id of the asset being borrowed.
  * @param amount_of_collateral the amount of the backing asset to add to your collateral
  *        position.  Make this negative to claim back some of your collateral.
  *        The backing asset is defined in the \c bitasset_options for the asset being borrowed.
  * @param broadcast true to broadcast the transaction on the network
  * @returns the signed transaction borrowing the asset
  */
      full_transaction borrow_asset(string borrower_name, string amount_to_borrow, string asset_symbol,
        string amount_of_collateral, bool broadcast = false);

      /** Cancel an existing order
       *
       * @param order_id the id of order to be cancelled
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction canceling the order
       */
      full_transaction cancel_order(object_id_type order_id, bool broadcast = false);

      /** Creates a new user-issued or market-issued asset.
       *
       * Many options can be changed later using \c update_asset()
       *
       * Right now this function is difficult to use because you must provide raw JSON data
       * structures for the options objects, and those include prices and asset ids.
       *
       * @param issuer the name or id of the account who will pay the fee and become the
       *               issuer of the new asset.  This can be updated later
       * @param symbol the ticker symbol of the new asset
       * @param precision the number of digits of precision to the right of the decimal point,
       *                  must be less than or equal to 12
       * @param common asset options required for all new assets.
       *               Note that core_exchange_rate technically needs to store the asset ID of
       *               this new asset. Since this ID is not known at the time this operation is
       *               created, create this price as though the new asset has instance ID 1, and
       *               the chain will overwrite it with the new asset's ID.
       * @param bitasset_opts options specific to BitAssets.  This may be null unless the
       *               \c market_issued flag is set in common.flags
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction creating a new asset
       */
      full_transaction create_asset(string issuer,
        string symbol,
        uint8_t precision,
        asset_options common,
        fc::optional<bitasset_options> bitasset_opts,
        bool broadcast = false);


      /** Creates a new user-issued or market-issued asset.
      *
      * Many options can be changed later using \c update_asset()
      *
      * Right now this function is difficult to use because you must provide raw JSON data
      * structures for the options objects, and those include prices and asset ids.
      *
      * @param issuer the name or id of the account who will pay the fee and become the
      *               issuer of the new asset.  This can be updated later
      * @param symbol the ticker symbol of the new asset
      * @param precision the number of digits of precision to the right of the decimal point,
      *                  must be less than or equal to 12
      * @param common asset options required for all new assets.
      *               Note that core_exchange_rate technically needs to store the asset ID of
      *               this new asset. Since this ID is not known at the time this operation is
      *               created, create this price as though the new asset has instance ID 1, and
      *               the chain will overwrite it with the new asset's ID.
      * @param bitasset_opts options specific to BitAssets.  This may be null unless the
      *               \c market_issued flag is set in common.flags
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction creating a new asset
      */
      full_transaction wallet_create_asset(string issuer,
        string symbol,
        uint8_t precision,
        share_type max_supply,
        share_type core_fee_paid,
        bool broadcast = false);
      full_transaction wallet_create_erc_asset(string issuer,
        string symbol,
        uint8_t precision,
        share_type max_supply,
        share_type core_fee_paid,
        std::string erc_address,
        bool broadcast = false);
      /** Issue new shares of an asset.
       *
       * @param to_account the name or id of the account to receive the new shares
       * @param amount the amount to issue, in nominal units
       * @param symbol the ticker symbol of the asset to issue
       * @param memo a memo to include in the transaction, readable by the recipient
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction issuing the new shares
       */
      full_transaction issue_asset(string to_account, string amount,
        string symbol,
        string memo,
        bool broadcast = false);

      /** Update the core options on an asset.
       * There are a number of options which all assets in the network use. These options are
       * enumerated in the asset_object::asset_options struct. This command is used to update
       * these options for an existing asset.
       *
       * @note This operation cannot be used to update BitAsset-specific options. For these options,
       * \c update_bitasset() instead.
       *
       * @param symbol the name or id of the asset to update
       * @param new_issuer if changing the asset's issuer, the name or id of the new issuer.
       *                   null if you wish to remain the issuer of the asset
       * @param new_options the new asset_options object, which will entirely replace the existing
       *                    options.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the asset
       */
      full_transaction update_asset(const string& account, const std::string& symbol,
        const std::string& description,
        bool broadcast = false);

      /** Update the options specific to a BitAsset.
       *
       * BitAssets have some options which are not relevant to other asset types. This operation is used to update those
       * options an an existing BitAsset.
       *
       * @see update_asset()
       *
       * @param symbol the name or id of the asset to update, which must be a market-issued asset
       * @param new_options the new bitasset_options object, which will entirely replace the existing
       *                    options.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the bitasset
       */
      full_transaction update_bitasset(string symbol,
        bitasset_options new_options,
        bool broadcast = false);

      /** Update the set of feed-producing accounts for a BitAsset.
       *
       * BitAssets have price feeds selected by taking the median values of recommendations from a set of feed producers.
       * This command is used to specify which accounts may produce feeds for a given BitAsset.
       * @param symbol the name or id of the asset to update
       * @param new_feed_producers a list of account names or ids which are authorized to produce feeds for the asset.
       *                           this list will completely replace the existing list
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the bitasset's feed producers
       */
      full_transaction update_asset_feed_producers(string symbol,
        flat_set<string> new_feed_producers,
        bool broadcast = false);

      /** Publishes a price feed for the named asset.
       *
       * Price feed providers use this command to publish their price feeds for market-issued assets. A price feed is
       * used to tune the market for a particular market-issued asset. For each value in the feed, the median across all
       * committee_member feeds for that asset is calculated and the market for the asset is configured with the median of that
       * value.
       *
       * The feed object in this command contains three prices: a call price limit, a short price limit, and a settlement price.
       * The call limit price is structured as (collateral asset) / (debt asset) and the short limit price is structured
       * as (asset for sale) / (collateral asset). Note that the asset IDs are opposite to eachother, so if we're
       * publishing a feed for USD, the call limit price will be CORE/USD and the short limit price will be USD/CORE. The
       * settlement price may be flipped either direction, as long as it is a ratio between the market-issued asset and
       * its collateral.
       *
       * @param publishing_account the account publishing the price feed
       * @param symbol the name or id of the asset whose feed we're publishing
       * @param feed the price_feed object containing the three prices making up the feed
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction updating the price feed for the given asset
       */
      full_transaction publish_asset_feed(string publishing_account,
        string symbol,
        price_feed feed,
        bool broadcast = false);

      full_transaction publish_normal_asset_feed(string publishing_account,
        string symbol,
        price_feed feed,
        bool broadcast = false);
      /** Pay into the fee pool for the given asset.
       *
       * User-issued assets can optionally have a pool of the core asset which is
       * automatically used to pay transaction fees for any transaction using that
       * asset (using the asset's core exchange rate).
       *
       * This command allows anyone to deposit the core asset into this fee pool.
       *
       * @param from the name or id of the account sending the core asset
       * @param symbol the name or id of the asset whose fee pool you wish to fund
       * @param amount the amount of the core asset to deposit
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction funding the fee pool
       */
      full_transaction fund_asset_fee_pool(string from,
        string symbol,
        string amount,
        bool broadcast = false);

      /** Burns the given user-issued asset.
       *
       * This command burns the user-issued asset to reduce the amount in circulation.
       * @note you cannot burn market-issued assets.
       * @param from the account containing the asset you wish to burn
       * @param amount the amount to burn, in nominal units
       * @param symbol the name or id of the asset to burn
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction burning the asset
       */
      full_transaction reserve_asset(string from,
        string amount,
        string symbol,
        bool broadcast = false);

      /** Forces a global settling of the given asset (black swan or prediction markets).
       *
       * In order to use this operation, asset_to_settle must have the global_settle flag set
       *
       * When this operation is executed all balances are converted into the backing asset at the
       * settle_price and all open margin positions are called at the settle price.  If this asset is
       * used as backing for other bitassets, those bitassets will be force settled at their current
       * feed price.
       *
       * @note this operation is used only by the asset issuer, \c settle_asset() may be used by
       *       any user owning the asset
       *
       * @param symbol the name or id of the asset to force settlement on
       * @param settle_price the price at which to settle
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction settling the named asset
       */
      full_transaction global_settle_asset(string symbol,
        price settle_price,
        bool broadcast = false);

      /** Schedules a market-issued asset for automatic settlement.
       *
       * Holders of market-issued assests may request a forced settlement for some amount of their asset. This means that
       * the specified sum will be locked by the chain and held for the settlement period, after which time the chain will
       * choose a margin posision holder and buy the settled asset using the margin's collateral. The price of this sale
       * will be based on the feed price for the market-issued asset being settled. The exact settlement price will be the
       * feed price at the time of settlement with an offset in favor of the margin position, where the offset is a
       * blockchain parameter set in the global_property_object.
       *
       * @param account_to_settle the name or id of the account owning the asset
       * @param amount_to_settle the amount of the named asset to schedule for settlement
       * @param symbol the name or id of the asset to settlement on
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction settling the named asset
       */
      full_transaction settle_asset(string account_to_settle,
        string amount_to_settle,
        string symbol,
        bool broadcast = false);

      /** Whitelist and blacklist accounts, primarily for transacting in whitelisted assets.
       *
       * Accounts can freely specify opinions about other accounts, in the form of either whitelisting or blacklisting
       * them. This information is used in chain validation only to determine whether an account is authorized to transact
       * in an asset type which enforces a whitelist, but third parties can use this information for other uses as well,
       * as long as it does not conflict with the use of whitelisted assets.
       *
       * An asset which enforces a whitelist specifies a list of accounts to maintain its whitelist, and a list of
       * accounts to maintain its blacklist. In order for a given account A to hold and transact in a whitelisted asset S,
       * A must be whitelisted by at least one of S's whitelist_authorities and blacklisted by none of S's
       * blacklist_authorities. If A receives a balance of S, and is later removed from the whitelist(s) which allowed it
       * to hold S, or added to any blacklist S specifies as authoritative, A's balance of S will be frozen until A's
       * authorization is reinstated.
       *
       * @param authorizing_account the account who is doing the whitelisting
       * @param account_to_list the account being whitelisted
       * @param new_listing_status the new whitelisting status
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction changing the whitelisting status
       */
      full_transaction whitelist_account(string authorizing_account,
        string account_to_list,
        account_whitelist_operation::account_listing new_listing_status,
        bool broadcast = false);

      /** Creates a wallfacer_member object owned by the given account.
       *
       * An account can have at most one wallfacer_member object.
       *
       * @param owner_account the name or id of the account which is creating the wallfacer_member
       * @param url a URL to include in the wallfacer_member record in the blockchain.  Clients may
       *            display this when showing a list of wallfacer_members.  May be blank.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering a wallfacer_member
       */
      full_transaction create_wallfacer_member(string account, bool broadcast = false);

      /** Creates a committee_member object owned by the given account.
      *
      * An account can have at most one committee_member object.
      *
      * @param proposing the name or id of the account which is updating the wallfacer_member
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction registering a committee_member
      */
      full_transaction update_wallfacer_formal(string proposing_account, map<account_id_type, account_id_type> replace_queue,
        int64_t expiration_time,
        bool broadcast = false);

      /** Resigns a wallfacer_member object owned by the given account.
      *
      * An account can have at most one wallfacer_member object.
      *
      * @param owner_account the name or id of the account which is resigning the wallfacer_member
      * @param broadcast true to broadcast the transaction on the network
      * @returns the signed transaction registering a wallfacer_member
      */
      full_transaction resign_wallfacer_member(string proposing_account, string account,
        int64_t expiration_time, bool broadcast /* = false */);

      /** Lists all witnesses registered in the blockchain.
       * This returns a list of all account names that own witnesses, and the associated witness id,
       * sorted by name.  This lists witnesses whether they are currently voted in or not.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all witnesss,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last witness name returned as the \c lowerbound for the next \c list_witnesss() call.
       *
       * @param lowerbound the name of the first witness to return.  If the named witness does not exist,
       *                   the list will start at the witness that comes after \c lowerbound
       * @param limit the maximum number of witnesss to return (max: 1000)
       * @returns a list of witnesss mapping witness names to witness ids
       */
      map<string, miner_id_type>       list_miners(const string& lowerbound, uint32_t limit);

      /** Lists all committee_members registered in the blockchain.
       * This returns a list of all account names that own committee_members, and the associated committee_member id,
       * sorted by name.  This lists committee_members whether they are currently voted in or not.
       *
       * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all committee_members,
       * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
       * the last committee_member name returned as the \c lowerbound for the next \c list_wallfacer_members() call.
       *
       * @param lowerbound the name of the first committee_member to return.  If the named committee_member does not exist,
       *                   the list will start at the committee_member that comes after \c lowerbound
       * @param limit the maximum number of committee_members to return (max: 1000)
       * @returns a list of committee_members mapping committee_member names to committee_member ids
       */
      map<string, wallfacer_member_id_type>       list_wallfacer_members(const string& lowerbound, uint32_t limit);

      /** Lists all committee_members registered in the blockchain.
      * This returns a list of all account names that own committee_members, and the associated committee_member id,
      * sorted by name.  This lists committee_members whether they are currently voted in or not.
      *
      * Use the \c lowerbound and limit parameters to page through the list.  To retrieve all committee_members,
      * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
      * the last committee_member name returned as the \c lowerbound for the next \c list_wallfacer_members() call.
      *
      * @param lowerbound the name of the first committee_member to return.  If the named committee_member does not exist,
      *                   the list will start at the committee_member that comes after \c lowerbound
      * @param limit the maximum number of committee_members to return (max: 1000)
      * @returns a list of committee_members mapping committee_member names to committee_member ids
      */
      map<string, wallfacer_member_id_type>       list_all_wallfacers(const string& lowerbound, uint32_t limit);


      /** Returns information about the given witness.
       * @param owner_account the name or id of the witness account owner, or the id of the witness
       * @returns the information about the witness stored in the block chain
       */
      miner_object get_miner(string owner_account);

      /** Returns information about the given committee_member.
       * @param owner_account the name or id of the committee_member account owner, or the id of the committee_member
       * @returns the information about the committee_member stored in the block chain
       */
      wallfacer_member_object get_wallfacer_member(string owner_account);

      /** Creates a miner object owned by the given account.
       *
       * An account can have at most one witness object.
       *
       * @param owner_account the name or id of the account which is creating the witness
       * @param url a URL to include in the miner record in the blockchain.  Clients may
       *            display this when showing a list of miner.  May be blank.
       * @param broadcast true to broadcast the transaction on the network
       * @returns the signed transaction registering a witness
       */
      full_transaction create_miner(string owner_account,
        string url,
        bool broadcast = false);

      /**
       * Update a witness object owned by the given account.
       *
       * @param witness The name of the witness's owner account.  Also accepts the ID of the owner account or the ID of the witness.
       * @param url Same as for create_witness.  The empty string makes it remain the same.
       * @param block_signing_key The new block signing public key.  The empty string makes it remain the same.
       * @param broadcast true if you wish to broadcast the transaction.
       */
      full_transaction update_witness(string witness_name,
        string url,
        string block_signing_key,
        bool broadcast = false);


      /**
       * Create a worker object.
       *
       * @param owner_account The account which owns the worker and will be paid
       * @param work_begin_date When the work begins
       * @param work_end_date When the work ends
       * @param daily_pay Amount of pay per day (NOT per maint interval)
       * @param name Any text
       * @param url Any text
       * @param worker_settings {"type" : "burn"|"refund"|"vesting", "pay_vesting_period_days" : x}
       * @param broadcast true if you wish to broadcast the transaction.
       */
      full_transaction create_worker(
        string owner_account,
        time_point_sec work_begin_date,
        time_point_sec work_end_date,
        share_type daily_pay,
        string name,
        string url,
        variant worker_settings,
        bool broadcast = false
      );

      /**
       * Update your votes for a worker
       *
       * @param account The account which will pay the fee and update votes.
       * @param worker_vote_delta {"vote_for" : [...], "vote_against" : [...], "vote_abstain" : [...]}
       * @param broadcast true if you wish to broadcast the transaction.
       */
      full_transaction update_worker_votes(
        string account,
        worker_vote_delta delta,
        bool broadcast = false
      );

      /**
       * Get information about a vesting balance object.
       *
       * @param account_name An account name, account ID, or vesting balance object ID.
       */
      vector< vesting_balance_object_with_info > get_vesting_balances(string account_name);

      /**
       * Withdraw a vesting balance.
       *
       * @param witness_name The account name of the witness, also accepts account ID or vesting balance ID type.
       * @param amount The amount to withdraw.
       * @param asset_symbol The symbol of the asset to withdraw.
       * @param broadcast true if you wish to broadcast the transaction
       */
      full_transaction withdraw_vesting(
        string witness_name,
        string amount,
        string asset_symbol,
        bool broadcast = false);

      /** Vote for a given committee_member.
       *
       * An account can publish a list of all committee_memberes they approve of.  This
       * command allows you to add or remove committee_memberes from this list.
       * Each account's vote is weighted according to the number of shares of the
       * core asset owned by that account at the time the votes are tallied.
       *
       * @note you cannot vote against a committee_member, you can only vote for the committee_member
       *       or not vote for the committee_member.
       *
       * @param voting_account the name or id of the account who is voting with their shares
       * @param committee_member the name or id of the committee_member' owner account
       * @param approve true if you wish to vote in favor of that committee_member, false to
       *                remove your vote in favor of that committee_member
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote for the given committee_member
       */
      full_transaction vote_for_committee_member(string voting_account,
        string committee_member,
        bool approve,
        bool broadcast = false);

      /** Vote for a given witness.
       *
       * An account can publish a list of all witnesses they approve of.  This
       * command allows you to add or remove witnesses from this list.
       * Each account's vote is weighted according to the number of shares of the
       * core asset owned by that account at the time the votes are tallied.
       *
       * @note you cannot vote against a witness, you can only vote for the witness
       *       or not vote for the witness.
       *
       * @param voting_account the name or id of the account who is voting with their shares
       * @param witness the name or id of the witness' owner account
       * @param approve true if you wish to vote in favor of that witness, false to
       *                remove your vote in favor of that witness
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote for the given witness
       */
      full_transaction vote_for_witness(string voting_account,
        string witness,
        bool approve,
        bool broadcast = false);

      /** Set the voting proxy for an account.
       *
       * If a user does not wish to take an active part in voting, they can choose
       * to allow another account to vote their stake.
       *
       * Setting a vote proxy does not remove your previous votes from the blockchain,
       * they remain there but are ignored.  If you later null out your vote proxy,
       * your previous votes will take effect again.
       *
       * This setting can be changed at any time.
       *
       * @param account_to_modify the name or id of the account to update
       * @param voting_account the name or id of an account authorized to vote account_to_modify's shares,
       *                       or null to vote your own shares
       *
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote proxy settings
       */
      full_transaction set_voting_proxy(string account_to_modify,
        optional<string> voting_account,
        bool broadcast = false);

      /** Set your vote for the number of witnesses and committee_members in the system.
       *
       * Each account can voice their opinion on how many committee_members and how many
       * witnesses there should be in the active committee_member/active witness list.  These
       * are independent of each other.  You must vote your approval of at least as many
       * committee_members or witnesses as you claim there should be (you can't say that there should
       * be 20 committee_members but only vote for 10).
       *
       * There are maximum values for each set in the blockchain parameters (currently
       * defaulting to 1001).
       *
       * This setting can be changed at any time.  If your account has a voting proxy
       * set, your preferences will be ignored.
       *
       * @param account_to_modify the name or id of the account to update
       * @param number_of_committee_members the number
       *
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed transaction changing your vote proxy settings
       */
      full_transaction set_desired_miner_and_wallfacer_member_count(string account_to_modify,
        uint16_t desired_number_of_witnesses,
        uint16_t desired_number_of_committee_members,
        bool broadcast = false);
      std::map<transaction_id_type, signed_transaction> get_crosschain_transaction(int type);
      std::map<transaction_id_type, signed_transaction> get_crosschain_transaction_by_block_num(const string& symbol,
        const uint32_t& start_block_num,
        const uint32_t& stop_block_num,
        int crosschain_trx_state);
      std::vector<crosschain_trx_object> get_account_crosschain_transaction(string account_address, string trx_id);
      std::map<transaction_id_type, signed_transaction> get_coldhot_transaction(const int& type);
      std::map<transaction_id_type, signed_transaction> get_coldhot_transaction_by_blocknum(const string& symbol,
        const uint32_t& start_block_num,
        const uint32_t& stop_block_num,
        int crosschain_trx_state);
      std::map<transaction_id_type, signed_transaction> get_withdraw_crosschain_without_sign_transaction();
      void wallfacer_sign_crosschain_transaction(const string& trx_id, const string& wallfacer);
      void wallfacer_sign_coldhot_transaction(const string& tx_id, const string& wallfacer, const string& keyfile, const string& decryptkey);
      void wallfacer_sign_eths_multi_account_create_trx(const string& tx_id, const string& wallfacer, const string& keyfile, const string& decryptkey);
      void wallfacer_sign_eths_final_trx(const string& tx_id, const string& wallfacer);
      void wallfacer_changer_eth_singer_trx(const string wallfacer, const string txid, const string& newaddress, const int64_t& expiration_time, bool broadcast);
      void wallfacer_changer_eth_coldhot_singer_trx(const string wallfacer, const string txid, const string& newaddress, const int64_t& expiration_time, const string& keyfile, const string& decryptkey, bool broadcast);
      void wallfacer_sign_eths_coldhot_final_trx(const string& tx_id, const string& wallfacer, const string& keyfile, const string& decryptkey);
      /** Signs a transaction.
       *
       * Given a fully-formed transaction that is only lacking signatures, this signs
       * the transaction with the necessary keys and optionally broadcasts the transaction
       * @param tx the unsigned transaction
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false);

      /** Returns an uninitialized object representing a given blockchain operation.
       *
       * This returns a default-initialized object of the given type; it can be used
       * during early development of the wallet when we don't yet have custom commands for
       * creating all of the operations the blockchain supports.
       *
       * Any operation the blockchain supports can be created using the transaction builder's
       * \c add_operation_to_builder_transaction() , but to do that from the CLI you need to
       * know what the JSON form of the operation looks like.  This will give you a template
       * you can fill in.  It's better than nothing.
       *
       * @param operation_type the type of operation to return, must be one of the
       *                       operations defined in `graphene/chain/operations.hpp`
       *                       (e.g., "global_parameters_update_operation")
       * @return a default-constructed operation of the given type
       */
      operation get_prototype_operation(string operation_type);

      /** Creates a transaction to propose a parameter change.
       *
       * Multiple parameters can be specified if an atomic change is
       * desired.
       *
       * @param proposing_account The account paying the fee to propose the tx
       * @param expiration_time Timestamp specifying when the proposal will either take effect or expire.
       * @param changed_values The values to change; all other chain parameters are filled in with default values
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      full_transaction propose_parameter_change(
        const string& proposing_account,
        const variant_object& changed_values,
        const int64_t& expiration_time,
        bool broadcast = false);

      full_transaction propose_coin_destory(
        const string& proposing_account,
        fc::time_point_sec expiration_time,
        const variant_object& destory_values,
        bool broadcast = false);

      full_transaction propose_wallfacer_pledge_change(
        const string& proposing_account,
        fc::time_point_sec expiration_time,
        const variant_object& changed_values,
        bool broadcast = false);
      full_transaction propose_pay_back_asset_rate_change(
        const string& proposing_account,
        const variant_object& changed_values,
        const int64_t& expiration_time,
        bool broadcast = false
      );
      /** Propose a fee change.
       *
       * @param proposing_account The account paying the fee to propose the tx
       * @param expiration_time Timestamp specifying when the proposal will either take effect or expire.
       * @param changed_values Map of operation type to new fee.  Operations may be specified by name or ID.
       *    The "scale" key changes the scale.  All other operations will maintain current values.
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      full_transaction propose_fee_change(
        const string& proposing_account,
        const variant_object& changed_values,
        int64_t expiration_time,
        bool broadcast = false);
      std::vector<lockbalance_object> get_account_lock_balance(const string& account)const;

      std::vector<wallfacer_lock_balance_object> get_wallfacer_lock_balance(const string& miner)const;
      std::vector<lockbalance_object> get_miner_lock_balance(const string& miner)const;
      std::vector<acquired_crosschain_trx_object> get_acquire_transaction(const int & type, const string & trxid);
      wallfacer_member_object get_eth_signer(const string& symbol, const string& address);
      /** Approve or disapprove a proposal.
       *
       * @param fee_paying_account The account paying the fee for the op.
       * @param proposal_id The proposal to modify.
       * @param delta Members contain approvals to create or remove.  In JSON you can leave empty members undefined.
       * @param broadcast true if you wish to broadcast the transaction
       * @return the signed version of the transaction
       */
      full_transaction approve_proposal(
        const string& fee_paying_account,
        const string& proposal_id,
        const approval_delta& delta,
        bool broadcast /* = false */
      );
      full_transaction approve_referendum(
        const string& fee_paying_account,
        const string& referendum_id,
        const approval_delta& delta,
        bool broadcast /* = false */
      );
      //get current proposal proposed by proposer
      vector<proposal_object>  get_proposal(const string& proposer);
      vector<proposal_object>  get_proposal_for_voter(const string& voter);
      vector<referendum_object> get_referendum_for_voter(const string& voter);
      order_book get_order_book(const string& base, const string& quote, unsigned limit = 50);
      /*
      void dbg_make_uia(string creator, string symbol);
      void dbg_make_mia(string creator, string symbol);*/
      void dbg_push_blocks(std::string src_filename, uint32_t count);
      void dbg_generate_blocks(std::string debug_wif_key, uint32_t count);
      void dbg_stream_json_objects(const std::string& filename);
      void dbg_update_object(fc::variant_object update);

      void flood_network(string prefix, uint32_t number_of_transactions);

      void network_add_nodes(const vector<string>& nodes);
      vector< variant > network_get_connected_peers();
      fc::variant_object network_get_info();
      // contract wallet apis
      full_transaction register_contract(const string& caller_account_name, const string& gas_price, const string& gas_limit, const string& contract_filepath);
      full_transaction register_contract_like(const string& caller_account_name, const string& gas_price, const string& gas_limit, const string& base);
      std::pair<asset, share_type> register_contract_testing(const string& caller_account_name, const string& contract_filepath);

      std::string register_native_contract(const string& caller_account_name, const string& gas_price, const string& gas_limit, const string& native_contract_key);
      std::pair<asset, share_type> register_native_contract_testing(const string& caller_account_name, const string& native_contract_key);

      full_transaction invoke_contract(const string& caller_account_name, const string& gas_price, const string& gas_limit, const string& contract_address_or_name, const string& contract_api, const string& contract_arg);
      std::pair<asset, share_type> invoke_contract_testing(const string& caller_account_name, const string& contract_address_or_name, const string& contract_api, const string& contract_arg);

      string invoke_contract_offline(const string& caller_account_name, const string& contract_address_or_name, const string& contract_api, const string& contract_arg);
      full_transaction upgrade_contract(const string& caller_account_name, const string& gas_price, const string& gas_limit, const string& contract_address, const string& contract_name, const string& contract_desc);
      std::pair<asset, share_type> upgrade_contract_testing(const string& caller_account_name, const string& contract_address, const string& contract_name, const string& contract_desc);
      ContractEntryPrintable get_contract_info(const string& contract_address_or_name)const;
      ContractEntryPrintable get_simple_contract_info(const string& contract_address_or_name)const;
      full_transaction transfer_to_contract(string from,
        string to,
        string amount,
        string asset_symbol,
        const string& param,
        const string& gas_price,
        const string& gas_limit,
        bool broadcast = false);
      std::pair<asset, share_type> transfer_to_contract_testing(string from,
        string to,
        string amount,
        string asset_symbol,
        const string& param);

      vector<asset> get_contract_balance(const string& contract_address) const;
      vector<contract_invoke_result_object> get_contract_invoke_object(const std::string&);
      vector<string> get_contract_addresses_by_owner(const std::string&);
      vector<ContractEntryPrintable> get_contracts_by_owner(const std::string&);
      vector<contract_hash_entry> get_contracts_hash_entry_by_owner(const std::string&);
      vector<contract_event_notify_object> get_contract_events(const std::string&);

      vector<contract_event_notify_object> get_contract_events_in_range(const  std::string&, uint64_t start, uint64_t range)const;
      vector<contract_blocknum_pair> get_contract_registered(const uint32_t block_num = 0);
      vector<contract_blocknum_pair> get_contract_storage_changed(const uint32_t block_num = 0);
      vector<transaction_id_type> get_contract_history(const string& contract_id, uint64_t start = 0, uint64_t end = UINT64_MAX);
      full_transaction create_contract_transfer_fee_proposal(const string& proposer, share_type fee_rate, int64_t expiration_time, bool broadcast = false);
      // end contract wallet apis
      // begin script wallet apis
      std::string add_script(const string& script_path);
      vector<script_object>list_scripts();
      void remove_script(const string& script_hash);
      bool bind_script_to_event(const string& script_hash, const string& contract, const string& event_name);
      bool remove_event_handle(const string& script_hash, const string& contract, const string& event_name);
      // end script wallet apis
      /**
       *  Used to transfer from one set of blinded balances to another
       */
      blind_confirmation blind_transfer_help(string from_key_or_label,
        string to_key_or_label,
        string amount,
        string symbol,
        bool broadcast = false,
        bool to_temp = false);

      full_transaction refund_request(const string& refund_account, const string txid, bool broadcast = false);
      full_transaction refund_uncombined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction refund_combined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction cancel_eth_sign_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction wallfacer_pass_combined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction wallfacer_change_acquire_trx(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction wallfacer_pass_coldhot_combined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction eth_cancel_fail_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction cancel_coldhot_eth_fail_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction cancel_coldhot_uncombined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction cancel_coldhot_combined_transaction(const string wallfacer, const string txid, const int64_t& expiration_time, bool broadcast = false);
      full_transaction cancel_cold_hot_uncreate_transaction(const string& proposer, const string& trxid, const int64_t& exception_time, bool broadcast = false);
      full_transaction transfer_from_cold_to_hot(const string& proposer, const string& from_account, const string& to_account, const string& amount, const string& asset_symbol, const string& memo, const int64_t& exception_time, bool broadcast = true);
      vector<optional<multisig_address_object>> get_multi_account_wallfacer(const string & multi_address, const string& symbol)const;
      vector<optional<account_binding_object>> get_binding_account(const string& account, const string& symbol) const;
      full_transaction account_change_for_crosschain(const string& proposer, const string& symbol, const string& hot, const string& cold, int64_t expiration_time, bool broadcast = false);
      full_transaction withdraw_from_link(const string& account, const string& symbol, int64_t amount, bool broadcast = true);
      full_transaction update_asset_private_keys(const string& from_account, const string& symbol, const string& out_key_file, const string& encrypt_key, bool broadcast = true);
      full_transaction update_asset_private_keys_with_brain_key(const string& from_account, const string& symbol, const string& out_key_file, const string& encrypt_key, bool broadcast = true);
      full_transaction update_asset_private_with_coldkeys(const string& from_account, const string& symbol, const string& cold_address, const string& cold_pubkey, bool broadcast);
      full_transaction update_asset_private_with_keys(const string& from_account, const string& symbol, const string& hot_address, const string& hot_pubkey, const string& cold_address, const string& cold_pubkey, bool broadcast);
      full_transaction bind_tunnel_account(const string& link_account, const string& tunnel_account, const string& symbol, bool broadcast = false);
      full_transaction bind_tunnel_account_with_script(const string& link_account, const string& tunnel_account, const string& script, const string& symbol, bool broadcast = false);
      crosschain_prkeys wallet_create_crosschain_symbol(const string& symbol);
      crosschain_prkeys wallet_create_crosschain_symbol_with_brain_key(const string& symbol);
      crosschain_prkeys create_crosschain_symbol(const string& symbol);
      crosschain_prkeys create_crosschain_symbol_with_brain_key(const string& symbol);
      crosschain_prkeys create_crosschain_symbol_cold(const string &symbol, const string& out_key_file, const string& encrypt_key);
      full_transaction set_balance_for_addr(const string& account, const address& addr, const asset& balance, bool broadcast = false);
      full_transaction unbind_tunnel_account(const string& link_account, const string& tunnel_account, const string& symbol, bool broadcast = false);
      full_transaction unbind_tunnel_account_with_script(const string& link_account, const string& tunnel_account, const string& script, const string& symbol, bool broadcast = false);
      std::map<string, std::function<string(fc::variant, const fc::variants&)>> get_result_formatters() const;
      vector<multisig_asset_transfer_object> get_multisig_asset_tx() const;
      full_transaction sign_multi_asset_trx(const string& account, multisig_asset_transfer_id_type id, const string& wallfacer, bool broadcast = false);
      vector<optional<multisig_address_object>> get_multi_address_obj(const string& symbol, const account_id_type& wallfacer) const;
      vector<optional<multisig_account_pair_object>> get_multisig_account_pair(const string& symbol) const;
      optional<multisig_account_pair_object> get_multisig_account_pair_by_id(const multisig_account_pair_id_type& id) const;
      optional<multisig_address_object> get_current_multi_address_obj(const string& symbol, const account_id_type& wallfacer) const;
      optional<multisig_account_pair_object> get_current_multi_address(const string& symbol) const;
      full_transaction create_guarantee_order(const string& account, const string& asset_orign, const string& asset_target, const string& symbol, bool broadcast = false);
      full_transaction cancel_guarantee_order(const guarantee_object_id_type id, bool broadcast = false);
      vector<optional<guarantee_object>> list_guarantee_order(const string& chain_type, bool all = true);
      vector<optional<guarantee_object>> get_my_guarantee_order(const string& account, bool all = true);
      fc::variant get_transaction(transaction_id_type id)const;
      fc::variant_object decoderawtransaction(const string& raw_trx, const string& symbol);
      fc::variant_object createrawtransaction(const string& from, const string& to, const string& amount, const string& symbol);
      string signrawtransaction(const string& from, const string& symbol, const fc::variant_object& trx, bool broadcast = true);
      string signrawmultransaction(const string& from, const string& to, const string& symbol, const fc::variant_object& trx, const string& password, const string& file);
      fc::variant_object combinemultisigtransaction(const fc::variant_object& trx, const fc::flat_set<string>& hexs, const string& symbol, bool broadcast = true);
      vector<transaction_id_type> list_transactions(uint32_t blocknum = 0, uint32_t nums = -1) const;
      void set_guarantee_id(const guarantee_object_id_type id);
      void remove_guarantee_id();
      void load_new_wallet(const fc::path & wallet, const string& password);
      optional<guarantee_object> get_guarantee_order(const guarantee_object_id_type id);
      full_transaction wallfacer_appointed_publisher(const string& account, const account_id_type publisher, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_cancel_publisher(const string& account, const account_id_type publisher, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_appointed_crosschain_fee(const string& account, const share_type fee, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_appointed_withdraw_limit(const string& account, const share_type limit, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_change_eth_gas_price(const string& account, const string& gas_price, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_appointed_lockbalance_wallfacer(const string& account, const std::map<string, asset>& lockbalance, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_determine_withdraw_deposit(const string& account, bool can, const string& symbol, int64_t expiration_time, bool broadcast = true);
      full_transaction wallfacer_determine_block_payment(const string& account, const std::map<uint32_t, uint32_t>& blocks_pays, int64_t expiration_time, bool broadcast = true);
      full_transaction proposal_block_address(const string& account, const fc::flat_set<address>& block_addr, int64_t expiration_time, bool broadcast = true);
      full_transaction proposal_cancel_block_address(const string& account, const fc::flat_set<address>& block_addr, int64_t expiration_time, bool broadcast = true);
      full_transaction miner_referendum_for_wallfacer(const string& miner, const string& amount, const map<account_id_type, account_id_type>& replacement, bool broadcast = true);
      full_transaction referendum_accelerate_pledge(const referendum_id_type referendum_id, const string& amount, bool broadcast = true);
      full_transaction add_whiteOperation(const string& proposer, const address& addr, const fc::flat_set<int>& ops, int64_t expiration_time, bool broadcast = true);
      full_transaction remove_whiteOperation(const string& proposer, const address& addr, int64_t expiration_time, bool broadcast = true);
      full_transaction create_vote(const string& proposer, const string& title, const vector<string>& options, int64_t expiration, bool broadcast = true);
      full_transaction cast_vote(const string& caster, const vote_object_id_type& id, const int index, bool broadcast = true);
      optional<whiteOperationList_object> get_whiteOperation(const string& account) const;
      vector<transaction_id_type> get_pending_transactions() const;
      optional<account_object> get_account_by_addr(const address& addr) const;
      map<public_key_type, address> create_multisignature_address(const string& account, const fc::flat_set<public_key_type>& pubs, int required, bool broadcast = true);
      map<account_id_type, vector<asset>> get_miner_lockbalance_info(const string& account);
      public_key_type get_pubkey_from_priv(const string& privkey);
      public_key_type get_pubkey_from_account(const string& account);
      string sign_multisig_trx(const address& addr, const string& trx);
      signed_transaction decode_multisig_transaction(const string& trx);
      variant_object  get_multisig_address(const address& addr);
      full_transaction set_miner_pledge_pay_back_rate(const string& miner, int pledge_pay_back_rate, bool broadcast = true);
      full_transaction correct_chain_data(const string& payer, vector<address> addresses, bool broadcast = true);
      fc::uint128_t get_pledge() const;
      flat_set< miner_id_type> list_active_miners();
      vector<optional< eth_multi_account_trx_object>> get_eth_multi_account_trx(const int & mul_acc_tx_state);
      fc::signal<void(bool)> lock_changed;
      std::shared_ptr<detail::wallet_api_impl> my;
      void encrypt_keys();
      fc::string get_first_contract_address();
      map<string, crosschain_prkeys> decrypt_coldkeys(const string& key, const string& file);
      //miner
      void start_miner(bool);

      void start_mining(const vector<string>& accts);

      //localnode
      void witness_node_stop();

      //ntp
      std::map<std::string, fc::ntp_info> get_ntp_info();
      void ntp_update_time();

      //master_key
      bool set_brain_key(string key, const int next = 1);
      brain_key_usage_info dump_brain_key_usage_info(const string& password);
      address wallet_create_account_with_brain_key(const string& name);
      map<string, int> list_address_indexes(string& password);
      string derive_wif_key(const string& brain_key, int index, const string& symbol);

      void send_coldhot_transfer_with_sign(const string& tx_id, const string& wallfacer, const string& siging);
      string get_coldhot_trx_sig(const string& tx_id, const string& wallfacer, const string& keyfile, const string& decryptkey);
      fc::variant extra_imp(const fc::variant_object& param_list);
      void set_gas_limit_in_block(const share_type& new_limit);
      contract_storage_view get_contract_storage(const address& contract_address, const string& storage_name);
      vector<fc::variant> get_votes(const string& account) const;
      /*void testaaa1() {}
      void testaaa2() {}
      void testaaa3() {}
      void testaaa4() {}
      void testaaa5() {}
      void testaaa6() {}
      void testaaa7() {}
      void testaaa8() {}
      void testaaa9() {}
      void testaaa10() {}
      void testaaa11() {}
      void testaaa12() {}
      void testaaa13() {}*/
    };

  }
}

FC_REFLECT(graphene::wallet::key_label, (label)(key))
FC_REFLECT(graphene::wallet::blind_balance, (amount)(from)(to)(one_time_key)(blinding_factor)(commitment)(used))
FC_REFLECT(graphene::wallet::blind_confirmation::output, (label)(pub_key)(decrypted_memo)(confirmation)(auth)(confirmation_receipt))
FC_REFLECT(graphene::wallet::blind_confirmation, (trx)(outputs))

FC_REFLECT(graphene::wallet::plain_keys, (keys)(crosschain_keys)(checksum))
FC_REFLECT(graphene::wallet::crosschain_prkeys, (addr)(pubkey)(wif_key))
FC_REFLECT(graphene::wallet::wallet_data,
(chain_id)
(my_accounts)
(my_scripts)
(cipher_keys)
(cipher_keys_extend)
(extra_keys)
(mining_accounts)
(pending_transactions)
(pending_account_registrations)(pending_miner_registrations)
(pending_account_updation)
(labeled_keys)
(blind_receipts)
(ws_server)
(ws_user)
(ws_password)
)
FC_REFLECT(graphene::wallet::brain_key_usage_info,
(key)
(next)
(used_indexes)
)
FC_REFLECT(graphene::wallet::brain_key_info,
(brain_priv_key)
(wif_priv_key)
(pub_key)
)

FC_REFLECT(graphene::wallet::exported_account_keys, (account_name)(encrypted_private_keys)(public_keys))

FC_REFLECT(graphene::wallet::exported_keys, (password_checksum)(account_keys))

FC_REFLECT(graphene::wallet::blind_receipt,
(date)(from_key)(from_label)(to_key)(to_label)(amount)(memo)(control_authority)(data)(used)(conf))

FC_REFLECT(graphene::wallet::approval_delta,
(active_approvals_to_add)
(active_approvals_to_remove)
(owner_approvals_to_add)
(owner_approvals_to_remove)
(key_approvals_to_add)
(key_approvals_to_remove)
)

FC_REFLECT(graphene::wallet::worker_vote_delta,
(vote_for)
(vote_against)
(vote_abstain)
)

FC_REFLECT_DERIVED(graphene::wallet::signed_block_with_info, (graphene::chain::signed_block),
(number)(block_id)(signing_key)(reward)(transaction_ids))



FC_REFLECT(graphene::wallet::operation_detail,
(memo)(description)(op))

FC_API(graphene::wallet::wallet_api,
  (help)
  (info)
  (about)
  (extra_imp)
  (is_locked)
  (is_new)
  (get_address_pay_back_balance)
  (obtain_pay_back_balance)
  (obtain_bonus_balance)
  (wallfacer_pass_combined_transaction)
  (wallfacer_pass_coldhot_combined_transaction)
  (lock)
  (unlock)
  (set_password)
  (dump_private_keys)
  (dump_private_key)
  (list_my_accounts)
  (get_addr_balances)
  (get_account_balances)
  (list_assets)
  (import_key)
  (register_account)
  (upgrade_account)
  (wallet_create_account)
  (wallfacer_change_eth_gas_price)
  (get_eth_multi_account_trx)
  (get_transaction_id)
  (create_asset)
  (get_acquire_transaction)
  (update_asset)
  (update_bitasset)
  (update_asset_feed_producers)
  (publish_asset_feed)
  (publish_normal_asset_feed)
  (get_asset)
  (get_asset_imp)
  (create_wallfacer_member)
  (get_miner)
  (get_wallfacer_member)
  (list_miners)
  (list_wallfacer_members)
  (list_all_wallfacers)
  (create_miner)
  (set_desired_miner_and_wallfacer_member_count)
  (get_account)
  (change_account_name)
  (remove_local_account)
  (get_account_id)
  (get_block)
  (change_acquire_plugin_num)
  (get_account_count)
  (get_account_history)
  (is_public_key_registered)
  (get_global_properties)
  (get_dynamic_global_properties)
  (get_object)
  (get_private_key)
  (load_wallet_file)
  (save_wallet_file)
  (wallfacer_change_acquire_trx)
  (serialize_transaction)
  (sign_transaction)
  (get_prototype_operation)
  (propose_wallfacer_pledge_change)
  (propose_pay_back_asset_rate_change)
  (propose_parameter_change)
  (propose_fee_change)
  (approve_proposal)
  (network_add_nodes)
  (network_get_connected_peers)
  (get_public_key)
  (send_coldhot_transfer_with_sign)
  (get_coldhot_trx_sig)
  (transfer_to_address)
  (transfer_to_account)
  (get_account_addr)
  //(get_proposal)
  (get_proposal_for_voter)
  (wallfacer_lock_balance)
  (foreclose_balance_from_miner)
  (wallfacer_foreclose_balance)
  (update_wallfacer_formal)
  (get_account_lock_balance)
  (get_wallfacer_lock_balance)
  (get_miner_lock_balance)
  (refund_request)
  (cancel_cold_hot_uncreate_transaction)
  (transfer_from_cold_to_hot)
  (get_multi_account_wallfacer)
  (get_multisig_asset_tx)
  (sign_multi_asset_trx)
  (get_binding_account)
  (withdraw_cross_chain_transaction)
  (refund_uncombined_transaction)
  (refund_combined_transaction)
  (cancel_coldhot_uncombined_transaction)
  (cancel_coldhot_combined_transaction)
  (eth_cancel_fail_transaction)
  (cancel_coldhot_eth_fail_transaction)
  (transfer_wallfacer_multi_account)
  (get_withdraw_crosschain_without_sign_transaction)
  (get_coldhot_transaction)
  (get_crosschain_transaction)
  (get_crosschain_transaction_by_block_num)
  (get_coldhot_transaction_by_blocknum)
  (get_multi_address_obj)
  (wallet_create_asset)
  (wallet_create_erc_asset)
  (create_crosschain_symbol)
  (create_crosschain_symbol_cold)
  (bind_tunnel_account)
  (unbind_tunnel_account)
  (update_asset_private_keys)
  (update_asset_private_keys_with_brain_key)
  (update_asset_private_with_coldkeys)
  (get_multisig_account_pair)
  (wallfacer_sign_crosschain_transaction)
  (wallfacer_sign_coldhot_transaction)
  (wallfacer_sign_eths_multi_account_create_trx)
  (wallfacer_sign_eths_final_trx)
  (wallfacer_changer_eth_singer_trx)
  (wallfacer_changer_eth_coldhot_singer_trx)
  (wallfacer_sign_eths_coldhot_final_trx)
  (account_change_for_crosschain)
  (get_current_multi_address_obj)
  (get_current_multi_address)
  (register_contract)
  (register_native_contract)
  (register_contract_like)
  (invoke_contract)
  (invoke_contract_offline)
  (upgrade_contract)
  (get_contract_info)
  (get_simple_contract_info)
  (transfer_to_contract)
  (get_contract_balance)
  (get_contract_addresses_by_owner)
  (get_contracts_by_owner)
  (get_contracts_hash_entry_by_owner)
  (get_contract_events)
  (get_contract_events_in_range)
  (get_contract_history)
  (create_guarantee_order)
  (list_guarantee_order)
  (set_guarantee_id)
  (cancel_guarantee_order)
  (invoke_contract_testing)
  (transfer_to_contract_testing)
  (register_contract_testing)
  (register_native_contract_testing)
  (upgrade_contract_testing)
  (get_contract_registered)
  (get_contract_storage_changed)
  (create_contract_transfer_fee_proposal)
  (get_transaction)
  (list_transactions)
  (dump_crosschain_private_key)
  (dump_crosschain_private_keys)
  (wallet_create_crosschain_symbol)
  (wallet_create_crosschain_symbol_with_brain_key)
  (import_crosschain_key)
  (decoderawtransaction)
  (createrawtransaction)
  (signrawtransaction)
  (get_my_guarantee_order)
  (get_contract_invoke_object)
  (get_guarantee_order)
  (wallfacer_appointed_publisher)
  (wallfacer_appointed_crosschain_fee)
  (remove_guarantee_id)
  (network_get_info)
  (start_miner)
  (get_account_crosschain_transaction)
  (witness_node_stop)
  (get_bonus_balance)
  (wallfacer_appointed_lockbalance_wallfacer)
  (get_miner_lockbalance_info)
  (wallfacer_cancel_publisher)
  (wallfacer_determine_withdraw_deposit)
  (lightwallet_broadcast)
  (lightwallet_get_refblock_info)
  (create_multisignature_address)
  (get_first_contract_address)
  (get_pubkey_from_priv)
  (sign_multisig_trx)
  (wallfacer_determine_block_payment)
  (transfer_from_to_address)
  (combine_transaction)
  (get_multisig_address)
  (set_miner_pledge_pay_back_rate)
  (list_active_miners)
  (decode_multisig_transaction)
  (get_pubkey_from_account)
  (get_eth_signer)
  (miner_referendum_for_wallfacer)
  (get_referendum_for_voter)
  (referendum_accelerate_pledge)
  (approve_referendum)
  (proposal_block_address)
  (proposal_cancel_block_address)
  (decrypt_coldkeys)
  (get_account_by_addr)
  (start_mining)
  (foreclose_balance_from_miners)
  (lock_balance_to_miners)
  (lock_balance_to_miner)
  (cancel_eth_sign_transaction)
  (wallet_create_account_with_brain_key)
  (get_pending_transactions)
  (update_asset_private_with_keys)
  (set_gas_limit_in_block)
  (bind_tunnel_account_with_script)
  (unbind_tunnel_account_with_script)
  (get_whiteOperation)
  (get_contract_storage)
  (signrawmultransaction)
  (combinemultisigtransaction)
  //(correct_chain_data)
  (get_votes)
  (create_vote)
  (cast_vote)
  (name_transfer_to_address)
  (confirm_name_transfer)
  (undertaker_customize)
  (confirm_undertaker)
  (build_transaction)
  (load_new_wallet)
  (wallfacer_appointed_withdraw_limit)
  (get_ntp_info)
  (ntp_update_time)
)
