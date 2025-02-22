/*=============================================================================
Copyright (c) 2001-2011 Joel de Guzman

Distributed under the Boost Software License, Version 1.0. (See accompanying
file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/
#if !defined(SPIRIT_EXPECTATION_FAILURE_JULY_19_2016)
#define SPIRIT_EXPECTATION_FAILURE_JULY_19_2016

#if defined(_MSC_VER)
#pragma once
#endif

#include <boost/spirit/home/support/info.hpp>

#include <stdexcept>

namespace boost { namespace spirit { namespace qi {
    template <typename Iterator>
    struct expectation_failure : std::runtime_error
    {
        expectation_failure(Iterator first_, Iterator last_, info const& what)
            : std::runtime_error("boost::spirit::qi::expectation_failure")
            , first(first_), last(last_), what_(what)
        {}
        ~expectation_failure() BOOST_NOEXCEPT_OR_NOTHROW {}

        Iterator first;
        Iterator last;
        info what_;
    };
}}}

#endif
