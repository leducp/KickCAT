#include "kickcat/CoE/EsiParser.h"
#include "kickcat/OS/Time.h"

#include "kickcat/TapSocket.h"

using namespace kickcat;

int main()
{
    TapSocket socketA(true);
    TapSocket socketB;
    TapSocket socketC;

    socketA.open("yolo");
    socketB.open("yolo");
    //socketC.open("yolo");


    uint8_t buffer[512];
    socketB.setTimeout(20ms);
    int r = socketB.read(buffer, 512);
    printf("read: %d\n", r);

    int w = socketA.write((uint8_t*)"Hello World", 12);
    socketB.setTimeout(-1ns);
    r = socketB.read(buffer, 512);
    printf("written: %d\n", w);
    printf("read: %d\n", r);
    if (r > 0)
    {
        printf("cool : %.*s\n", r, buffer);
    }

/*

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
*/

    return 0;
}
