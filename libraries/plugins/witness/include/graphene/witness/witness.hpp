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

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/protocol/types.hpp>

#include <fc/thread/future.hpp>
#include <mutex>

#define WITENESS_NODE_VERSION  "1.3.6"
#define WITENESS_CFG_SIGNATURE "7C4B74BFEA6"

namespace graphene { namespace miner_plugin {

namespace block_production_condition
{
   enum block_production_condition_enum
   {
      produced = 0,
      not_synced = 1,
      not_my_turn = 2,
      not_time_yet = 3,
      no_private_key = 4,
      low_participation = 5,
      lag = 6,
      consecutive = 7,
      exception_producing_block = 8,
      stopped = 9
   };
}

class miner_plugin :public graphene::app::plugin {
public:
   ~miner_plugin() {
      try {
         if( _block_production_task.valid() || !_block_production_task.canceled())
            _block_production_task.cancel_and_wait(__FUNCTION__);
      } catch(fc::canceled_exception&) {
         //Expected exception. Move along.
      } catch(fc::exception& e) {
         edump((e.to_detail_string()));
      }
   }

   std::string plugin_name()const override;

   virtual void plugin_set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   void set_block_production(bool allow) { _production_enabled = allow; }
   void set_miner(const map<chain::miner_id_type, fc::ecc::private_key>&, bool add = false);

   virtual void plugin_initialize( const boost::program_options::variables_map& options ) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;

private:
   void schedule_production_loop();
   block_production_condition::block_production_condition_enum block_production_loop();
   block_production_condition::block_production_condition_enum maybe_produce_block( fc::mutable_variant_object& capture );
   fc::variant check_generate_multi_addr(chain::miner_id_type miner,fc::ecc::private_key prk);
   void check_eths_generate_multi_addr(chain::miner_id_type miner, fc::ecc::private_key prk);
   void check_multi_transfer(chain::miner_id_type miner, fc::ecc::private_key prk);
   boost::program_options::variables_map _options;
   volatile bool _production_enabled = false;
   bool _consecutive_production_enabled = false;
   uint32_t _required_miner_participation = 33 * GRAPHENE_1_PERCENT;
   uint32_t _production_skip_flags = graphene::chain::database::skip_nothing;

   std::map<chain::public_key_type, fc::ecc::private_key> _private_keys;
   std::set<chain::miner_id_type> _miners;
   std::mutex _miner_lock;
   fc::future<void> _block_production_task;
   int min_gas_price;
};

} } //graphene::miner_plugin
