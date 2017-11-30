/* IFC parsing has two sides:
 * - parsing the step file, gathering all entities, resolving attribute types and direct
 *   attribute relationships
 * - parsing the express file, adding inverse relationships to the ifc file, as well as additional
 *   type information
 * Is it possible to generate code which does this type and relation enrichment based on the parsed
 * Express schema? This might be faster than working generic, and opens the possibility for better
 * type support.
 */

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/repository/include/qi_seek.hpp>
#include <boost/phoenix/phoenix.hpp>

using boost::spirit::qi::phrase_parse;
using boost::spirit::qi::space;
using boost::spirit::qi::space_type;
using boost::spirit::qi::double_;
using boost::spirit::qi::char_;
using boost::spirit::qi::rule;

namespace express_step
{
    namespace fusion = boost::fusion;

struct step_header
{
    std::vector<std::string> file_description;
};

template<typename t>
void print_param(t parm)
{
    std::cout << "Parameter: " << parm << "\n";
}

template<>
void print_param(std::vector<char> parm)
{
    std::string out(parm.begin(), parm.end());
    std::cout << "Parameter: " << out << "\n";
}

/* fill step lines into map: id -> entity
* entity is class, fields:
* - a vector<attributes> field
*   attribute is probably a boost:variant (fusion)
* - inverse relations field (vector<uint id>)
* - type (string), for faster comparisons preferably enum generated from schema
* when parsing entity references, access
*/
//currently parses a STEP header
//TODO fill a header data structure with header informations
//TODO find meaning of attributes of header fields
template <typename Iterator, typename Skipper>
struct step : boost::spirit::qi::grammar<Iterator, Skipper>
{
    step() : step::base_type(header)
    {
        using boost::spirit::ascii::char_;
        using boost::spirit::long_;
        using boost::spirit::lit;
        using boost::spirit::lexeme;

        isoline = lit("ISO-10303-21") > ';';
        header_begin = lit("HEADER") > ';';
        header_end = lit("ENDSEC") > ';';
        file_description = lit("FILE_DESCRIPTION") >> parameter_list;
        file_name = lit("FILE_NAME") >> parameter_list;
        file_schema = lit("FILE_SCHEMA") >> parameter_list;
        header_line = (file_description | file_name | file_schema) >> ';';
        //this can basically read a totally mangled header up until the beginning of the 
        //payload section, as long as the keywords are there.
        header = +(isoline | header_begin | header_line | header_end) > lit("DATA") > ';' ;

        parameter_list = '(' >> parameter >> *(',' >> parameter) >> ')';
        parameter = simple_type | parameter_list | '$' | '*';
        simple_type = simple_string | simple_int;
        simple_string = (lexeme['\'' >> *(char_ - '\'') >> '\''])[&print_param<std::vector<char>>];
        simple_int = long_[&print_param<long>];
    }
    boost::spirit::qi::rule<Iterator, Skipper> isoline;
    boost::spirit::qi::rule<Iterator, Skipper> header_begin;
    boost::spirit::qi::rule<Iterator, Skipper> header_line;
    boost::spirit::qi::rule<Iterator, Skipper> header_end;
    boost::spirit::qi::rule<Iterator, Skipper> file_description;
    boost::spirit::qi::rule<Iterator, Skipper> file_schema;
    boost::spirit::qi::rule<Iterator, Skipper> file_name;
    boost::spirit::qi::rule<Iterator, Skipper> header;

    boost::spirit::qi::rule<Iterator, Skipper> parameter_list;
    boost::spirit::qi::rule<Iterator, Skipper > parameter;
    boost::spirit::qi::rule<Iterator, Skipper > simple_type;
    boost::spirit::qi::rule<Iterator, Skipper > simple_string;
    boost::spirit::qi::rule<Iterator, Skipper > simple_int;

};

//Use this grammar as skip parameter to parse, to skip over whitespace and comments
//anywhere in the file
template<typename Iterator>
struct space_comment_skipper : public boost::spirit::qi::grammar<Iterator>
{
    space_comment_skipper() : space_comment_skipper::base_type(skipper)
    {
        skipper = boost::spirit::ascii::space | ("/*" >> boost::spirit::repository::seek["*/"]);
    }
    boost::spirit::qi::rule<Iterator> skipper;
};

}

int main(int argc, char const* argv[])
{
    char const* filename;
    if (argc > 1)
    {
        filename = argv[1];
    }
    else
    {
        std::cerr << "Error: No input file provided." << std::endl;
        return 1;
    }
    std::ifstream in(filename, std::ios_base::in);
    if (!in)
    {
        std::cerr << "Error: Could not open input file: "
            << filename << std::endl;
        return 1;
    }
    std::string storage; // We will read the contents here.
    in.unsetf(std::ios::skipws); // No white space skipping!
    std::cout << "Opened file, copying data...\n";
    std::copy(
        std::istream_iterator<char>(in),
        std::istream_iterator<char>(),
        std::back_inserter(storage));
    std::cout << "Data copied, starting parsing...\n";

    auto first = storage.begin();
    auto end = storage.end();

    typedef std::string::const_iterator StringIterator;
    typedef express_step::space_comment_skipper<StringIterator> StepSkipper;
    typedef express_step::step<StringIterator, StepSkipper> StepParser;
    StepParser step_parser;
    StepSkipper step_skipper;

    bool parsed = phrase_parse(first, end, step_parser, step_skipper);
    std::cout << "Iterator at end: " << (first == end) << "; parsing result: " << parsed << "\n";
    return 0;
}

