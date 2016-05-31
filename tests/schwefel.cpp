#define BOOST_TEST_MODULE schwefel_test
#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>
#include <exception>
#include <iostream>
#include <string>

#include "../include/problem.hpp"
#include "../include/problems/schwefel.hpp"
#include "../include/problems/null_problem.hpp"
#include "../include/types.hpp"

using namespace pagmo;

BOOST_AUTO_TEST_CASE(schwefel_test)
{
    // Problem construction
    schwefel sch1{1u};
    schwefel sch3{3u};
    BOOST_CHECK_THROW(schwefel{0u}, std::invalid_argument);
    BOOST_CHECK_NO_THROW(problem{sch3});
    // Pick a few reference points
    vector_double x1 = {1.12};
    vector_double x3 = {-23.45,12.34,111.12};
    // Fitness test
    BOOST_CHECK_CLOSE(sch1.fitness(x1)[0], 418.0067810680098, 1e-13);
    BOOST_CHECK_CLOSE(sch1.fitness(x3)[0], 1338.0260195323838, 1e-13);
    // Bounds Test
    BOOST_CHECK((sch3.get_bounds() == std::pair<vector_double, vector_double>{{-500, -500, -500},{500, 500, 500}}));
    // Name and extra info tests
    BOOST_CHECK(sch3.get_name().find("Schwefel") != std::string::npos);
    // Best known test
    auto x_best = sch3.best_known();
    BOOST_CHECK((x_best == vector_double{420.9687, 420.9687, 420.9687}));

}

BOOST_AUTO_TEST_CASE(schwefel_serialization_test)
{
    problem p{schwefel{4u}};
    // Call objfun to increase the internal counters.
    p.fitness({1.,1.,1.,1.});
    // Store the string representation of p.
    std::stringstream ss;
    auto before = boost::lexical_cast<std::string>(p);
    // Now serialize, deserialize and compare the result.
    {
    cereal::JSONOutputArchive oarchive(ss);
    oarchive(p);
    }
    // Change the content of p before deserializing.
    p = problem{null_problem{}};
    {
    cereal::JSONInputArchive iarchive(ss);
    iarchive(p);
    }
    auto after = boost::lexical_cast<std::string>(p);
    BOOST_CHECK_EQUAL(before, after);
}
