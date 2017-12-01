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
#include <boost/fusion/include/adapt_struct.hpp>

namespace express_step
{
struct file_description_struct {
    std::vector<std::string> model_view; //index 0 is model view, all others comments
    std::string step_file_version; //Which STEP file standard is used. For IFC usually 2;1
};
struct file_name_struct {
    std::string file_name;
    std::string time_stamp; //file creation time, ISO8601 format
    std::vector<std::string> file_authors; //names and email-addresses
    std::vector<std::string> author_organizations; //organization of the file author
    std::string file_processor; //which software created the file
    std::string originating_system; //which system originally created the information in this file
    std::string authorization; //name and address whjo authorized this file
};
struct file_schema_struct {
    std::vector<std::string> schema_version; //should contain only one item
};
struct step_file
{
    struct file_description_struct file_description;
    struct file_name_struct file_name;
    struct file_schema_struct file_schema;
};
}

BOOST_FUSION_ADAPT_STRUCT(
    express_step::file_schema_struct,
    (std::vector<std::string>, schema_version)
)
BOOST_FUSION_ADAPT_STRUCT(
    express_step::file_description_struct,
    (std::vector<std::string>,model_view)
    (std::string,step_file_version)
)
BOOST_FUSION_ADAPT_STRUCT(
    express_step::file_name_struct,
    (std::string,file_name)
    (std::string,time_stamp)
    (std::vector<std::string>,file_authors)
    (std::vector<std::string>,author_organizations)
    (std::string,file_processor)
    (std::string,originating_system)
    (std::string,authorization)
)
BOOST_FUSION_ADAPT_STRUCT(
    express_step::step_file,
    (struct express_step::file_description_struct, file_description)
    (struct express_step::file_name_struct, file_name)
    (struct express_step::file_schema_struct, file_schema)
)

namespace express_step
{

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

//parses a STEP header
template <typename Iterator, typename Skipper>
struct step_header : boost::spirit::qi::grammar<Iterator, step_file(), Skipper>
{
    step_header() : step_header::base_type(header)
    {
        using boost::spirit::ascii::char_;
        using boost::spirit::lit;
        using boost::spirit::lexeme;
        using namespace boost::spirit::qi::labels; //for _val and _1

        isoline = lit("ISO-10303-21") > ';';
        header_begin = lit("HEADER") > ';';
        header_end = lit("ENDSEC") > ';';
        file_description %= lit("FILE_DESCRIPTION")
            > lit('(') > string_list > ','
            > simple_string > lit(')') > ';';
        file_name %= lit("FILE_NAME") > lit('(') 
            > simple_string > ','
            > simple_string > ','
            > string_list > ','
            > string_list > ','
            > simple_string > ','
            > simple_string > ','
            > simple_string
            >lit(')') > ';';
        file_schema %= lit("FILE_SCHEMA") > lit('(') > string_list  > lit(')') > ';';
        header_line %= (file_description >> file_name >> file_schema);
        //this can basically read a mangled header up until the beginning of the 
        //payload section, as long as the keywords are there.
        header = +(isoline | header_begin | header_line[_val = _1]) > header_end;

        //the nil sign ($) is also used instead of empty strings
        simple_string %= lexeme['\'' >> *(char_ - '\'') >> '\''] | '$';
        string_list %= (lit('(') > (simple_string % ',') > lit(')')) | '$';
    }
    boost::spirit::qi::rule<Iterator, Skipper> isoline;
    boost::spirit::qi::rule<Iterator, Skipper> header_begin;
    boost::spirit::qi::rule<Iterator, step_file(), Skipper> header_line;
    boost::spirit::qi::rule<Iterator, Skipper> header_end;
    boost::spirit::qi::rule<Iterator, file_description_struct(), Skipper> file_description;
    boost::spirit::qi::rule<Iterator, file_schema_struct(), Skipper> file_schema;
    boost::spirit::qi::rule<Iterator, file_name_struct(), Skipper> file_name;
    boost::spirit::qi::rule<Iterator, step_file(), Skipper> header;

    boost::spirit::qi::rule<Iterator, std::string(), Skipper > simple_string;
    boost::spirit::qi::rule<Iterator, std::vector<std::string>(), Skipper > string_list;
};

/* fill step lines into map: id -> entity
* entity is class, fields:
* - a vector<attributes> field
*   attribute is probably a boost:variant (fusion)
* - inverse relations field (vector<uint id>)
* - type (string), for faster comparisons preferably enum generated from schema
* when parsing entity references, access
*/
template <typename Iterator, typename Skipper>
struct step_data : boost::spirit::qi::grammar<Iterator, Skipper>
{
    step_data() : step_data::base_type(data)
    {
        using boost::spirit::ascii::char_;
        using boost::spirit::ascii::upper;
        using boost::spirit::ascii::digit;
        using boost::spirit::repository::seek;
        using boost::spirit::long_;
        using boost::spirit::lit;
        using boost::spirit::lexeme;
        using namespace boost::spirit::qi::labels; //for _val and _1

        data = seek["DATA"] > ';' >> (*data_line) >> data_end;
        data_end = lit("ENDSEC") > ';';
        entity_name %= +(upper | digit | '_');
        data_line = '#' > long_ > '=' > entity_name > *(char_ - ';') > ';';


        //parameter_list = '(' >> parameter >> *(',' >> parameter) >> ')';
        //parameter = simple_type | parameter_list | '$' | '*';
        //simple_type = simple_string | simple_int;
        simple_string %= (lexeme['\'' >> *(char_ - '\'') >> '\'']);
        //simple_int = long_[&print_param<long>];
    }
    boost::spirit::qi::rule<Iterator, Skipper> data_line;
    boost::spirit::qi::rule<Iterator, Skipper> data_end;
    boost::spirit::qi::rule<Iterator, std::string(), Skipper> entity_name;
    boost::spirit::qi::rule<Iterator, Skipper> data;

    //boost::spirit::qi::rule<Iterator, Skipper > parameter_list;
    //boost::spirit::qi::rule<Iterator, Skipper > parameter;
    //boost::spirit::qi::rule<Iterator, Skipper > simple_type;
    boost::spirit::qi::rule<Iterator, std::string(), Skipper > simple_string;
    //boost::spirit::qi::rule<Iterator, Skipper > simple_int;
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
        filename = argv[1];
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
    typedef express_step::step_header<StringIterator, StepSkipper> StepHeaderParser;
    typedef express_step::step_data<StringIterator, StepSkipper> StepDataParser;
    StepHeaderParser step_parser;
    StepDataParser step_data_parser;
    StepSkipper step_skipper;
    express_step::step_file file_contents;

    bool header_parsed = phrase_parse(first, end, step_parser, step_skipper, file_contents);
    bool data_parsed = phrase_parse(first, end, step_data_parser, step_skipper);
    bool end_parsed = phrase_parse(first, end, "END-ISO-10303-21" > boost::spirit::lit(';'), step_skipper);
    std::cout << "Iterator at end: " << (first == end) << "\n"
        << "header parsing result: " << header_parsed << "\n"
        << "data parsing result: " << data_parsed << "\n"
        << "end parsing result: " << end_parsed << "\n";
    return 0;
}

