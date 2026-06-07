#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/SIIParser.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/protocol.h"

using namespace kickcat;
namespace fs = std::filesystem;

// Replicates the slave-side SAFE_OP mapping resolution (PDO::parseAssignment +
// parsePdoMap, ETG.1000.6 Tables 74/75): every PDO in a SyncManager assignment
// must reference a mapping object whose entries all resolve to a dictionary entry.
static bool resolveAssignment(CoE::Dictionary& dict, uint16_t assign_idx, std::string& fail)
{
    auto [obj0, e0] = CoE::findObject(dict, assign_idx, 0);
    if (e0 == nullptr or e0->data == nullptr) { return true; }  // no such assignment

    uint8_t pdo_count = *static_cast<uint8_t*>(e0->data);
    char buf[64];
    for (uint8_t i = 1; i <= pdo_count; ++i)
    {
        auto [ao, ae] = CoE::findObject(dict, assign_idx, i);
        if (ae == nullptr or ae->data == nullptr) { fail = "assignment sub-index missing"; return false; }
        uint16_t pdo_idx = *static_cast<uint16_t*>(ae->data);

        auto [mo, me] = CoE::findObject(dict, pdo_idx, 0);
        if (me == nullptr or me->data == nullptr)
        {
            snprintf(buf, sizeof(buf), "mapping object 0x%04x missing", pdo_idx);
            fail = buf;
            return false;
        }
        uint8_t entry_count = *static_cast<uint8_t*>(me->data);
        for (uint8_t j = 1; j <= entry_count; ++j)
        {
            auto [eo, ee] = CoE::findObject(dict, pdo_idx, j);
            if (ee == nullptr or ee->data == nullptr) { fail = "mapping entry missing"; return false; }
            uint32_t m = *static_cast<uint32_t*>(ee->data);
            uint16_t index = (m & CoE::PDO::MAPPING_INDEX_MASK) >> CoE::PDO::MAPPING_INDEX_SHIFT;
            uint8_t  sub   = (m & CoE::PDO::MAPPING_SUB_MASK) >> CoE::PDO::MAPPING_SUB_SHIFT;
            if (index == 0) { continue; }  // padding gap
            auto [to, te] = CoE::findObject(dict, index, sub);
            if (te == nullptr)
            {
                snprintf(buf, sizeof(buf), "0x%04x -> 0x%04x:%u unresolved", pdo_idx, index, sub);
                fail = buf;
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("esi_validate");

    std::string path_arg;
    program.add_argument("path")
        .help("ESI XML directory to validate (parse + build SII + resolve PDO maps)")
        .store_into(path_arg);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl << program;
        return 2;
    }

    fs::path dir = path_arg;
    int files = 0, total = 0, built = 0, build_fail = 0, sii_ok = 0, sii_fail = 0;
    int map_ok = 0, map_fail = 0;

    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: survive a crash mid-run
    auto heartbeat = [](std::string const& s)
    {
        FILE* hb = std::fopen("/tmp/esi_cur.txt", "w");
        if (hb) { std::fputs(s.c_str(), hb); std::fclose(hb); }
    };

    std::vector<fs::path> xmls;
    for (auto& e : fs::recursive_directory_iterator(dir))
    {
        if (e.path().extension() == ".xml") { xmls.push_back(e.path()); }
    }
    std::sort(xmls.begin(), xmls.end());

    for (auto& path : xmls)
    {
        files++;
        heartbeat("PARSING " + path.filename().string());
        ESI::Parser p;
        std::vector<std::string> errs;
        std::vector<ESI::Device> devs;
        try { devs = p.loadAllDevices(path.string(), &errs); }
        catch (std::exception& ex) { printf("FILE-FAIL %s : %s\n", path.filename().string().c_str(), ex.what()); continue; }

        total += (int)devs.size() + (int)errs.size();
        built += (int)devs.size();
        build_fail += (int)errs.size();
        for (auto& er : errs) { printf("BUILD-FAIL %s | %s\n", path.filename().string().c_str(), er.c_str()); }

        for (auto& d : devs)
        {
            heartbeat(path.filename().string() + " | " + d.type);
            try
            {
                auto img = ESI::buildEepromImage(d);
                eeprom::SII s;
                s.parse(img.data(), img.size());
                bool ok = (img.size() >= 16)
                       && (s.info.product_code == d.product_code)
                       && (eeprom::computeInfoCRC(s.info) == s.info.crc);
                if (ok) { sii_ok++; }
                else { sii_fail++; printf("SII-BAD  %s | %s pc=0x%08x\n", path.filename().string().c_str(), d.type.c_str(), d.product_code); }

                CoE::materializeStorage(d.dictionary);
                std::string mfail;
                bool mok = resolveAssignment(d.dictionary, 0x1C12, mfail)
                        && resolveAssignment(d.dictionary, 0x1C13, mfail);
                if (mok) { map_ok++; }
                else { map_fail++; printf("MAP-FAIL %s | %s rev=0x%x : %s\n", path.filename().string().c_str(), d.type.c_str(), d.revision_no, mfail.c_str()); }
            }
            catch (std::exception& ex)
            {
                sii_fail++;
                printf("SII-THROW %s | %s : %s\n", path.filename().string().c_str(), d.type.c_str(), ex.what());
            }
        }
    }

    printf("\n=== SUMMARY ===\nfiles=%d devices=%d built=%d build_fail=%d sii_ok=%d sii_fail=%d map_ok=%d map_fail=%d\n",
           files, total, built, build_fail, sii_ok, sii_fail, map_ok, map_fail);
    return (build_fail == 0 && sii_fail == 0 && map_fail == 0) ? 0 : 1;
}
