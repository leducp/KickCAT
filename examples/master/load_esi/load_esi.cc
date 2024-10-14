#include "kickcat/CoE/EsiParser.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/OS/Time.h"

using namespace kickcat;

int main()
{
    CoE::EsiParser parser;

    nanoseconds t1 = since_epoch();
    CoE::Dictionary coe_dict = parser.load("ingenia_esi.xml");
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
