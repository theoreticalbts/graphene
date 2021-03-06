/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <graphene/chain/vesting_balance_object.hpp>

namespace graphene { namespace chain {

inline bool sum_below_max_shares(const asset& a, const asset& b)
{
   assert(GRAPHENE_MAX_SHARE_SUPPLY + GRAPHENE_MAX_SHARE_SUPPLY > GRAPHENE_MAX_SHARE_SUPPLY);
   return (a.amount              <= GRAPHENE_MAX_SHARE_SUPPLY)
       && (            b.amount  <= GRAPHENE_MAX_SHARE_SUPPLY)
       && ((a.amount + b.amount) <= GRAPHENE_MAX_SHARE_SUPPLY);
}

asset linear_vesting_policy::get_allowed_withdraw( const vesting_policy_context& ctx )const
{
    share_type allowed_withdraw = 0;

    if( ctx.now > begin_timestamp )
    {
        const auto elapsed_seconds = (ctx.now - begin_timestamp).to_seconds();
        assert( elapsed_seconds > 0 );

        if( elapsed_seconds >= vesting_cliff_seconds )
        {
            share_type total_vested = 0;
            if( elapsed_seconds < vesting_duration_seconds )
            {
                total_vested = (fc::uint128_t( begin_balance.value ) * elapsed_seconds / vesting_duration_seconds).to_uint64();
            }
            else
            {
                total_vested = begin_balance;
            }
            assert( total_vested >= 0 );

            const share_type withdrawn_already = begin_balance - ctx.balance.amount;
            assert( withdrawn_already >= 0 );

            allowed_withdraw = total_vested - withdrawn_already;
            assert( allowed_withdraw >= 0 );
        }
    }

    return asset( allowed_withdraw, ctx.amount.asset_id );
}

void linear_vesting_policy::on_deposit(const vesting_policy_context& ctx)
{
}

bool linear_vesting_policy::is_deposit_allowed(const vesting_policy_context& ctx)const
{
   return (ctx.amount.asset_id == ctx.balance.asset_id)
      && sum_below_max_shares(ctx.amount, ctx.balance);
}

void linear_vesting_policy::on_withdraw(const vesting_policy_context& ctx)
{
}

bool linear_vesting_policy::is_withdraw_allowed(const vesting_policy_context& ctx)const
{
   return (ctx.amount.asset_id == ctx.balance.asset_id)
          && (ctx.amount <= get_allowed_withdraw(ctx));
}

fc::uint128_t cdd_vesting_policy::compute_coin_seconds_earned(const vesting_policy_context& ctx)const
{
   assert(ctx.now >= coin_seconds_earned_last_update);
   int64_t delta_seconds = (ctx.now - coin_seconds_earned_last_update).to_seconds();
   assert(delta_seconds >= 0);

   fc::uint128_t delta_coin_seconds = ctx.balance.amount.value;
   delta_coin_seconds *= delta_seconds;

   fc::uint128_t coin_seconds_earned_cap = ctx.balance.amount.value;
   coin_seconds_earned_cap *= vesting_seconds;

   return std::min(coin_seconds_earned + delta_coin_seconds, coin_seconds_earned_cap);
}

void cdd_vesting_policy::update_coin_seconds_earned(const vesting_policy_context& ctx)
{
   coin_seconds_earned = compute_coin_seconds_earned(ctx);
   coin_seconds_earned_last_update = ctx.now;
}

asset cdd_vesting_policy::get_allowed_withdraw(const vesting_policy_context& ctx)const
{
   if(ctx.now <= start_claim)
      return asset(0, ctx.balance.asset_id);
   fc::uint128_t cs_earned = compute_coin_seconds_earned(ctx);
   fc::uint128_t withdraw_available = cs_earned / vesting_seconds;
   assert(withdraw_available <= ctx.balance.amount.value);
   return asset(withdraw_available.to_uint64(), ctx.balance.asset_id);
}

void cdd_vesting_policy::on_deposit(const vesting_policy_context& ctx)
{
   update_coin_seconds_earned(ctx);
}

void cdd_vesting_policy::on_deposit_vested(const vesting_policy_context& ctx)
{
   on_deposit(ctx);
   coin_seconds_earned += ctx.amount.amount.value * vesting_seconds;
}

void cdd_vesting_policy::on_withdraw(const vesting_policy_context& ctx)
{
   update_coin_seconds_earned(ctx);
   fc::uint128_t coin_seconds_needed = ctx.amount.amount.value;
   coin_seconds_needed *= vesting_seconds;
   // is_withdraw_allowed should forbid any withdrawal that
   // would trigger this assert
   assert(coin_seconds_needed <= coin_seconds_earned);

   coin_seconds_earned -= coin_seconds_needed;
}

bool cdd_vesting_policy::is_deposit_allowed(const vesting_policy_context& ctx)const
{
   return (ctx.amount.asset_id == ctx.balance.asset_id)
         && sum_below_max_shares(ctx.amount, ctx.balance);
}

bool cdd_vesting_policy::is_deposit_vested_allowed(const vesting_policy_context& ctx) const
{
   return is_deposit_allowed(ctx);
}

bool cdd_vesting_policy::is_withdraw_allowed(const vesting_policy_context& ctx)const
{
   return (ctx.amount <= get_allowed_withdraw(ctx));
}

#define VESTING_VISITOR(NAME, MAYBE_CONST)                    \
struct NAME ## _visitor                                       \
{                                                             \
   typedef decltype(                                          \
      std::declval<linear_vesting_policy>().NAME(             \
         std::declval<vesting_policy_context>())              \
     ) result_type;                                           \
                                                              \
   NAME ## _visitor(                                          \
      const asset& balance,                                   \
      const time_point_sec& now,                              \
      const asset& amount                                     \
     )                                                        \
   : ctx(balance, now, amount) {}                             \
                                                              \
   template< typename Policy >                                \
   result_type                                                \
   operator()(MAYBE_CONST Policy& policy) MAYBE_CONST         \
   {                                                          \
      return policy.NAME(ctx);                                \
   }                                                          \
                                                              \
   vesting_policy_context ctx;                                \
}

VESTING_VISITOR(on_deposit,);
VESTING_VISITOR(on_deposit_vested,);
VESTING_VISITOR(on_withdraw,);
VESTING_VISITOR(is_deposit_allowed, const);
VESTING_VISITOR(is_deposit_vested_allowed, const);
VESTING_VISITOR(is_withdraw_allowed, const);
VESTING_VISITOR(get_allowed_withdraw, const);

bool vesting_balance_object::is_deposit_allowed(const time_point_sec& now, const asset& amount)const
{
   return policy.visit(is_deposit_allowed_visitor(balance, now, amount));
}

bool vesting_balance_object::is_withdraw_allowed(const time_point_sec& now, const asset& amount)const
{
   bool result = policy.visit(is_withdraw_allowed_visitor(balance, now, amount));
   // if some policy allows you to withdraw more than your balance,
   //    there's a programming bug in the policy algorithm
   assert((amount <= balance) || (!result));
   return result;
}

void vesting_balance_object::deposit(const time_point_sec& now, const asset& amount)
{
   on_deposit_visitor vtor(balance, now, amount);
   policy.visit(vtor);
   balance += amount;
}

void vesting_balance_object::deposit_vested(const time_point_sec& now, const asset& amount)
{
   on_deposit_vested_visitor vtor(balance, now, amount);
   policy.visit(vtor);
   balance += amount;
}

bool vesting_balance_object::is_deposit_vested_allowed(const time_point_sec& now, const asset& amount) const
{
   return policy.visit(is_deposit_vested_allowed_visitor(balance, now, amount));
}

void vesting_balance_object::withdraw(const time_point_sec& now, const asset& amount)
{
   assert(amount <= balance);
   on_withdraw_visitor vtor(balance, now, amount);
   policy.visit(vtor);
   balance -= amount;
}

asset vesting_balance_object::get_allowed_withdraw(const time_point_sec& now)const
{
   asset amount = asset();
   return policy.visit(get_allowed_withdraw_visitor(balance, now, amount));
}

} } // graphene::chain
