#include "kickcat/CoE/EsiParser.h"
#include "kickcat/OS/Time.h"
#include <argparse/argparse.hpp>

using namespace kickcat;

int main(int argc, char const* argv[])
{
    argparse::ArgumentParser program("load_esi");

    std::string esi_file;
    program.add_argument("-f", "--file")
        .help("ESI XML file")
        .required()
        .store_into(esi_file);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    CoE::EsiParser parser;

    nanoseconds t1 = since_epoch();
    CoE::Dictionary coe_dict = parser.loadFile(esi_file);
    nanoseconds t2 = since_epoch();

    // dangerous lack of error checking.
    printf("Name of vendor: %s\n", parser.vendor());
    printf("Profile: %s\n",        parser.profile());


    printf("Load %ld object\n", coe_dict.size());
    for (auto const& entry : coe_dict)
    {
        printf("%s\n", CoE::toString(entry).c_str());
    }
    printf("Scan file in %fs\n", seconds_f(t2-t1).count());

    return 0;
}
