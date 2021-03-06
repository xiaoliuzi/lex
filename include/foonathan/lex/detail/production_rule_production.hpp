// Copyright (C) 2018-2019 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef FOONATHAN_LEX_PRODUCTION_RULE_PRODUCTION_HPP_INCLUDED
#define FOONATHAN_LEX_PRODUCTION_RULE_PRODUCTION_HPP_INCLUDED

#include <boost/mp11/set.hpp>

#include <foonathan/lex/detail/production_rule_base.hpp>
#include <foonathan/lex/parse_error.hpp>

namespace foonathan
{
namespace lex
{
    namespace production_rule
    {
        namespace detail
        {
            //=== traits ===//
            struct base_choice_rule : base_rule
            {};
            template <typename T>
            struct is_choice_rule : std::is_base_of<base_choice_rule, T>
            {};

            //=== rules ===//
            struct direct_recurse_production : base_rule
            {
                template <class Cont>
                struct parser : Cont
                {
                    using production = typename Cont::tlp;

                    template <typename Func>
                    static constexpr auto callback_return_type(int, Func& f)
                        -> parse_result<decltype(f.result_of(std::declval<production>()))>;

                    template <typename Func>
                    static constexpr auto callback_return_type(short, Func&)
                    {
                        return lex::detail::missing_callback_result_of<Func, production>{};
                    }

                    template <class TokenSpec, typename Func, typename... Args>
                    static constexpr auto parse(tokenizer<TokenSpec>& tokenizer, Func& f,
                                                Args&&... args)
                        -> decltype(
                            Cont::parse(tokenizer, f, static_cast<Args&&>(args)...,
                                        callback_return_type(0, f).template forward<production>()))
                    {
                        auto result = production::parse_self(tokenizer, f);
                        if (result.is_success())
                            return Cont::parse(tokenizer, f, static_cast<Args&&>(args)...,
                                               result.template forward<production>());
                        else
                            return {};
                    }
                };
            };

            template <class Production>
            struct recurse_production : base_rule
            {
                template <class Cont>
                struct parser : Cont
                {
                    static_assert(is_production<Production>::value,
                                  "only a production can be used in this context");
                    static_assert(!std::is_same<Production, typename Cont::tlp>::value,
                                  "no need to use recursion for self recursion");

                    template <typename Func, class TokenSpec>
                    static constexpr auto callback_return_type(int, Func& f,
                                                               tokenizer<TokenSpec>& tokenizer)
                        -> decltype(Production::parse(tokenizer, f));

                    template <typename Func, class TokenSpec>
                    static constexpr auto callback_return_type(short, Func&, tokenizer<TokenSpec>&)
                    {
                        return lex::detail::missing_callback_result_of<Func, Production>{};
                    }

                    template <class TokenSpec, typename Func, typename... Args>
                    static constexpr auto parse(tokenizer<TokenSpec>& tokenizer, Func& f,
                                                Args&&... args)
                        -> decltype(Cont::parse(
                            tokenizer, f, static_cast<Args&&>(args)...,
                            callback_return_type(0, f, tokenizer).template forward<Production>()))
                    {
                        auto result = Production::parse(tokenizer, f);
                        if (result.is_success())
                            return Cont::parse(tokenizer, f, static_cast<Args&&>(args)...,
                                               result.template forward<Production>());
                        else
                            return {};
                    }
                };
            };

            template <class Production>
            struct production : base_rule
            {
                static_assert(is_production<Production>::value,
                              "only a production can be used in this context");

                template <class Cont>
                struct parser_impl : Cont
                {
                    template <class TokenSpec, typename Func, typename... Args>
                    static constexpr auto parse(tokenizer<TokenSpec>& tokenizer, Func& f,
                                                Args&&... args)
                        -> decltype(Cont::parse(
                            tokenizer, f, static_cast<Args&&>(args)...,
                            Production::parse(tokenizer, f).template forward<Production>()))
                    {
                        auto result = Production::parse(tokenizer, f);
                        if (result.is_success())
                            return Cont::parse(tokenizer, f, static_cast<Args&&>(args)...,
                                               result.template forward<Production>());
                        else
                            return {};
                    }
                };

                // if we're having direct recursion, we need the recursion parser, it handles the
                // issue with the return type
                template <class Cont>
                using parser
                    = std::conditional_t<std::is_same<Production, typename Cont::tlp>::value,
                                         parser_for<direct_recurse_production, Cont>,
                                         parser_impl<Cont>>;
            };

            template <class... Rules>
            struct sequence : base_rule
            {
                static_assert(mp::mp_none_of<sequence, is_choice_rule>::value,
                              "a choice cannot be composed further");

                template <class Cont>
                using parser = parser_for<Rules..., Cont>;
            };

            template <class PeekRule, class Rule>
            struct choice_alternative : base_choice_rule
            {
                using peek_rule = PeekRule;
                using rule      = Rule;

                template <class TokenSpec>
                static constexpr bool peek(tokenizer<TokenSpec> tokenizer)
                {
                    // use alternative if rule matched
                    return is_rule_parsed<PeekRule>(tokenizer);
                }

                template <class Cont>
                using parser = parser_for<Rule, Cont>;
            };

            template <class... Choices>
            struct choice : base_choice_rule
            {
                static_assert(mp::mp_is_set<choice>::value, "duplicate alternatives in a choice");

                template <class Cont>
                struct parser : Cont
                {
                    using grammar = typename Cont::grammar;
                    using tlp     = typename Cont::tlp;

                    template <class R, class TokenSpec, typename Func>
                    static constexpr R parse_impl(choice<>, tokenizer<TokenSpec>& tokenizer,
                                                  Func& f)
                    {
                        auto error = exhausted_choice<grammar, tlp>(tlp{});
                        lex::detail::report_error(f, error, tokenizer);
                        return {};
                    }
                    template <class R, class Head, class... Tail, class TokenSpec, typename Func>
                    static constexpr R parse_impl(choice<Head, Tail...>,
                                                  tokenizer<TokenSpec>& tokenizer, Func& f)
                    {
                        if (Head::peek(tokenizer))
                            return parser_for<Head, Cont>::parse(tokenizer, f);
                        else
                            return parse_impl<R>(choice<Tail...>{}, tokenizer, f);
                    }

                    template <class TokenSpec, typename Func>
                    static constexpr auto parse(tokenizer<TokenSpec>& tokenizer, Func& f)
                    {
                        using return_type = std::common_type_t<decltype(
                            parser_for<Choices, Cont>::parse(tokenizer, f))...>;
                        return parse_impl<return_type>(choice<Choices...>{}, tokenizer, f);
                    }
                };
            };

            /// Choice, then Tail as often as possible.
            template <class Choice, class Tail>
            struct left_recursion_choice : base_choice_rule
            {
                template <class Cont>
                struct parser : Cont
                {
                    using grammar = typename Cont::grammar;
                    using tlp     = typename Cont::tlp;

                    template <class TokenSpec, typename Func>
                    static constexpr auto parse(tokenizer<TokenSpec>& tokenizer, Func& f)
                    {
                        // parse choice, then invoke callback
                        using choice_parser = parser_for<Choice, final_parser<grammar, tlp>>;
                        auto result         = choice_parser::parse(tokenizer, f);
                        if (result.is_unmatched())
                            // error, didn't match at all
                            return result;

                        // try to parse Tail as often as possible
                        while (true)
                        {
                            auto next_result
                                = try_parse<parser_for<Tail, Cont>>(tokenizer, f,
                                                                    result.template forward<tlp>());
                            if (next_result.is_unmatched())
                                break;
                            result = static_cast<decltype(next_result)&&>(next_result);
                        }

                        return result;
                    }
                };
            };

        } // namespace detail
    }     // namespace production_rule
} // namespace lex
} // namespace foonathan

#endif // FOONATHAN_LEX_PRODUCTION_RULE_PRODUCTION_HPP_INCLUDED
