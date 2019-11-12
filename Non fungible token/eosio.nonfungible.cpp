#include "eosio.nonfungible.hpp"
using namespace eosio;

ACTION nonfungible::create( name issuer, std::string sym ) {

	require_auth( _self );

	// Check if issuer account exists
	eosio_assert( is_account( issuer ), "issuer account does not exist");

        // Valid symbol
        asset supply(0, symbol( symbol_code( sym.c_str() ), 0) );

        auto symbol = supply.symbol;
        eosio_assert( symbol.is_valid(), "invalid symbol name" );

        // Check if symbol used is unique
	auto symbol_name = symbol.code().raw();
        currency_index currency_table( _self, symbol_name );
        auto existing_currency = currency_table.find( symbol_name );
        eosio_assert( existing_currency == currency_table.end(), "token with symbol already exists" );

        // Create new currency
        currency_table.emplace( _self, [&]( auto& currency ) {
           currency.supply = supply;
           currency.issuer = issuer;
        });
}

ACTION nonfungible::issue( name to,
                     asset quantity,
                     vector<string> uris,
		     string tkn_name,
                     string memo) {

	eosio_assert( is_account( to ), "to account does not exist");

        // e,g, Get EOS from 3 EOS
        auto symbol = quantity.symbol;
        eosio_assert( symbol.is_valid(), "invalid symbol name" );
        eosio_assert( symbol.precision() == 0, "quantity must be a whole number" );
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

	eosio_assert( tkn_name.size() <= 32, "name has more than 32 bytes" );

        // Ensure currency has been created
        auto symbol_name = symbol.code().raw();
        currency_index currency_table( _self, symbol_name );
        auto existing_currency = currency_table.find( symbol_name );
        eosio_assert( existing_currency != currency_table.end(), "token with symbol does not exist. create token before issue" );
        const auto& st = *existing_currency;

        // Ensure have issuer authorization and valid quantity
        require_auth( st.issuer );
        eosio_assert( quantity.is_valid(), "invalid quantity" );
        eosio_assert( quantity.amount > 0, "must issue positive quantity of nonfungible" );
        eosio_assert( symbol == st.supply.symbol, "symbol precision mismatch" );

        // Increase supply
	add_supply( quantity );

        // Check that number of tokens matches uri size
        eosio_assert( quantity.amount == uris.size(), "mismatch between number of tokens and uris provided" );

        // Mint nonfungibles
        for(auto const& uri: uris) {
            mint( to, st.issuer, asset{1, symbol}, uri, tkn_name);
        }

        // Add balance to account
        add_balance( to, quantity, st.issuer );
}


ACTION nonfungible::transferid( name	from,
                        name 	to,
                        id_type	id,
                        string	memo ) {
        // Ensure authorized to send from account
        eosio_assert( from != to, "cannot transfer to self" );
        require_auth( from );

        // Ensure 'to' account exists
        eosio_assert( is_account( to ), "to account does not exist");

	// Check memo size and print
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

        // Ensure token ID exists
        auto send_token = tokens.find( id );
        eosio_assert( send_token != tokens.end(), "token with specified ID does not exist" );

	// Ensure owner owns token
        eosio_assert( send_token->owner == from, "sender does not own token with specified ID");

	const auto& st = *send_token;

	// Notify both recipients
        require_recipient( from );
        require_recipient( to );

        // Transfer nonfungible from sender to receiver
        tokens.modify( send_token, from, [&]( auto& token ) {
	        token.owner = to;
        });

        // Change balance of both accounts
        sub_balance( from, st.value );
        add_balance( to, st.value, from );
}

ACTION nonfungible::transfer( name 	from,
                      name 	to,
                      asset	quantity,
                      string	memo ) {
        // Ensure authorized to send from account
        eosio_assert( from != to, "cannot transfer to self" );
        require_auth( from );

        // Ensure 'to' account exists
        eosio_assert( is_account( to ), "to account does not exist");

        // Check memo size and print
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

	eosio_assert( quantity.amount == 1, "cannot transfer quantity, not equal to 1" );

	auto symbl = tokens.get_index<"bysymbol"_n>();

	auto it = symbl.lower_bound(quantity.symbol.code().raw());

	bool found = false;
	id_type id = 0;
	for(; it!=symbl.end(); ++it){

		if( it->value.symbol == quantity.symbol && it->owner == from) {
			id = it->id;
			found = true;
			break;
		}
	}

	eosio_assert(found, "token does not exist");

	// Notify both recipients
        require_recipient( from );
	require_recipient( to );

	SEND_INLINE_ACTION( *this, transferid, {from, "active"_n}, {from, to, id, memo} );
}

void nonfungible::mint( name 	owner,
                name 	payer,
                asset 	value,
                string 	uri,
		string 	tkn_name) {
        // Add token with creator paying for usage
        tokens.emplace( payer, [&]( auto& token ) {
            token.id = tokens.available_primary_key();
            token.uri = uri;
            token.owner = owner;
            token.value = value;
	    token.tokenName = tkn_name;
        });
}

ACTION nonfungible::set payer(name payer, id_type id) {

	require_auth(payer);

	// Ensure token ID exists
	auto payer_token = tokens.find( id );
	eosio_assert( payer_token != tokens.end(), "token with specified ID does not exist" );

	// Ensure payer owns token
	eosio_assert( payer_token->owner == payer, "payer does not own token with specified ID");

	const auto& st = *payer_token;

	// Notify payer
	require_recipient( payer );

	
	// Set owner as  payer
	tokens.modify(payer_token, payer, [&](auto& token){
		token.id = st.id;
		token.uri = st.uri;
		token.owner = st.owner;
		token.value = st.value;
		token.tokenName = st.tokenName;
	});

	sub_balance( payer, st.value );
	add_balance( payer, st.value, payer );
}


ACTION nonfungible::burn( name owner, id_type token_id ) {

        require_auth( owner );

        // Find token to burn
        auto burn_token = tokens.find( token_id );
	eosio_assert( burn_token != tokens.end(), "token  does not exist" );
	eosio_assert( burn_token->owner == owner, "account does not have token" );

	asset burnt_supply = burn_token->value;

	// Remove token from tokens table
        tokens.erase( burn_token );

        // Lower balance from owner
        sub_balance( owner, burnt_supply );

        // Lower supply from currency
        sub_supply( burnt_supply );
}


void nonfungible::sub_balance( name owner, asset value ) {

	account_index from_acnts( _self, owner.value );
        const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
        eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );


        if( from.balance.amount == value.amount ) {
            from_acnts.erase( from );
        } else {
            from_acnts.modify( from, owner, [&]( auto& a ) {
                a.balance -= value;
            });
        }
}

void nonfungible::add_balance( name owner, asset value, name payer ) {

	account_index to_accounts( _self, owner.value );
        auto to = to_accounts.find( value.symbol.code().raw() );
        if( to == to_accounts.end() ) {
            to_accounts.emplace( payer, [&]( auto& a ){
                a.balance = value;
            });
        } else {
            to_accounts.modify( to, _self, [&]( auto& a ) {
                a.balance += value;
            });
        }
}

void nonfungible::sub_supply( asset quantity ) {

	auto symbol_name = quantity.symbol.code().raw();
        currency_index currency_table( _self, symbol_name );
        auto current_currency = currency_table.find( symbol_name );

        currency_table.modify( current_currency, _self, [&]( auto& currency ) {
            currency.supply -= quantity;
        });
}

void nonfungible::add_supply( asset quantity ) {

        auto symbol_name = quantity.symbol.code().raw();
        currency_index currency_table( _self, symbol_name );
        auto current_currency = currency_table.find( symbol_name );

        currency_table.modify( current_currency, name(0), [&]( auto& currency ) {
            currency.supply += quantity;
        });
}

EOSIO_DISPATCH( nonfungible, (create)(issue)(transfer)(transferid)(set""payer)(burn) )
