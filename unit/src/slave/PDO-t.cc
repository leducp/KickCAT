#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>

#include "mocks/ESC.h"
#include "kickcat/PDO.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/protocol.h"
#include "kickcat/protocol.h"

using namespace kickcat;
using namespace testing;

constexpr uint16_t PDO_IN_ADDR = 0x1400;
constexpr uint16_t PDO_OUT_ADDR = 0x1600;
constexpr uint16_t PDO_SIZE = 16;

static SyncManager::Register makeSM(uint16_t start, uint16_t length, uint8_t control)
{
    SyncManager::Register sm{};
    sm.start_address = start;
    sm.length = length;
    sm.control = control;
    sm.activate = SM_ACTIVATE_ENABLE;
    return sm;
}

static constexpr uint16_t smPdiAddr(uint8_t index)
{
    return static_cast<uint16_t>(reg::SYNC_MANAGER + index * sizeof(SyncManager::Register) + 7);
}

static uint32_t makeMappingEntry(uint16_t index, uint8_t sub, uint8_t bits)
{
    return (static_cast<uint32_t>(index) << CoE::PDO::MAPPING_INDEX_SHIFT) | (static_cast<uint32_t>(sub) << CoE::PDO::MAPPING_SUB_SHIFT) | bits;
}

class PDOTest : public ::testing::Test
{
public:
    NiceMock<MockESC> esc_{};
    PDO pdo_{&esc_};

    uint8_t input_[PDO_SIZE]{};
    uint8_t output_[PDO_SIZE]{};

    SyncManager::Register sm_pdo_in_ = makeSM(PDO_IN_ADDR, PDO_SIZE, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
    SyncManager::Register sm_pdo_out_ = makeSM(PDO_OUT_ADDR, PDO_SIZE, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE);
    SyncManager::Register sm_mbx_in_ = makeSM(0x1000, 256, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ);
    SyncManager::Register sm_mbx_out_ = makeSM(0x1200, 256, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE);
    SyncManager::Register sm_empty_{};

    void SetUp() override
    {
        pdo_.setInput(input_, PDO_SIZE);
        pdo_.setOutput(output_, PDO_SIZE);
        setupSmReads();
        // configureMapping needs the SM resolved by configure(); production maps after it too.
        pdo_.configure();
    }

    void setupSm(int idx, SyncManager::Register const &sm)
    {
        ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager::Register) * idx), _, sizeof(SyncManager::Register)))
            .WillByDefault(DoAll(
                Invoke([sm](uint16_t, void *ptr, uint16_t)
                       { std::memcpy(ptr, &sm, sizeof(SyncManager::Register)); }),
                Return(sizeof(SyncManager::Register))));
    }

    void setPdoLengths(uint16_t input, uint16_t output)
    {
        sm_pdo_in_.length = input;
        sm_pdo_out_.length = output;
        setupSmReads();
        ASSERT_EQ(0, pdo_.configure());
    }

    void setupSmReads()
    {
        // Conventional layout: SM2 = outputs (0x1C12), SM3 = inputs (0x1C13).
        setupSm(0, sm_mbx_in_);
        setupSm(1, sm_mbx_out_);
        setupSm(2, sm_pdo_out_);
        setupSm(3, sm_pdo_in_);
        setupSm(4, sm_empty_);

        // PDI control byte used by activateSm/deactivateSm:
        //   activateSm loop exits when bit0 == 0 → return 0
        //   deactivateSm loop exits when bit0 == 1 → return 1 (overridden per test)
        for (uint8_t i = 2; i <= 3; ++i)
        {
            ON_CALL(esc_, read(smPdiAddr(i), _, sizeof(uint8_t)))
                .WillByDefault(DoAll(
                    Invoke([](uint16_t, void *ptr, uint16_t)
                           { *static_cast<uint8_t *>(ptr) = 0; }),
                    Return(sizeof(uint8_t))));
        }
    }

    void configurePdo()
    {
        ASSERT_EQ(0, pdo_.configure());
    }

    void setupDeactivatePdi(uint8_t sm_index)
    {
        ON_CALL(esc_, read(smPdiAddr(sm_index), _, sizeof(uint8_t)))
            .WillByDefault(DoAll(
                Invoke([](uint16_t, void *ptr, uint16_t)
                       { *static_cast<uint8_t *>(ptr) = 1; }),
                Return(sizeof(uint8_t))));
    }
};

// ---- configure() ----

TEST_F(PDOTest, configure_success)
{
    ASSERT_EQ(0, pdo_.configure());
}

TEST_F(PDOTest, configure_mailbox_only_leaves_both_unused)
{
    SyncManager::Register mbx{};
    mbx.control = SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ;
    for (int i = 0; i < 5; ++i)
    {
        ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager::Register) * i), _, sizeof(SyncManager::Register)))
            .WillByDefault(DoAll(
                Invoke([mbx](uint16_t, void *ptr, uint16_t)
                       { std::memcpy(ptr, &mbx, sizeof(SyncManager::Register)); }),
                Return(sizeof(SyncManager::Register))));
    }
    ASSERT_EQ(0, pdo_.configure());
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.isConfigOk());
}

TEST_F(PDOTest, configure_input_only_leaves_output_unused)
{
    SyncManager::Register empty{};
    ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager::Register) * 2), _, sizeof(SyncManager::Register)))
        .WillByDefault(DoAll(
            Invoke([empty](uint16_t, void *ptr, uint16_t)
                   { std::memcpy(ptr, &empty, sizeof(SyncManager::Register)); }),
            Return(sizeof(SyncManager::Register))));

    ASSERT_EQ(0, pdo_.configure());
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.isConfigOk());
    ASSERT_TRUE(pdo_.hasInput());
    ASSERT_FALSE(pdo_.hasOutput());

    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    pdo_.activateOutput(true);
}

TEST_F(PDOTest, configure_finds_input_sm_beyond_sm4)
{
    setupSm(3, sm_empty_);
    setupSm(5, sm_pdo_in_);

    ASSERT_EQ(0, pdo_.configure());
    ASSERT_TRUE(pdo_.hasInput());
    ASSERT_TRUE(pdo_.hasOutput());
}

// ---- isConfigOk() ----

TEST_F(PDOTest, isConfigOk_no_error)
{
    configurePdo();
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.isConfigOk());
}

TEST_F(PDOTest, isConfigOk_invalid_input_length_mismatch)
{
    configurePdo();
    SyncManager::Register bad = sm_pdo_in_;
    bad.length = PDO_SIZE + 1;
    ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager::Register) * 3), _, sizeof(SyncManager::Register)))
        .WillByDefault(DoAll(
            Invoke([bad](uint16_t, void *ptr, uint16_t)
                   { std::memcpy(ptr, &bad, sizeof(SyncManager::Register)); }),
            Return(sizeof(SyncManager::Register))));
    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.isConfigOk());
}

TEST_F(PDOTest, isConfigOk_invalid_output_length_mismatch)
{
    configurePdo();
    SyncManager::Register bad = sm_pdo_out_;
    bad.length = PDO_SIZE + 1;
    ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager::Register) * 2), _, sizeof(SyncManager::Register)))
        .WillByDefault(DoAll(
            Invoke([bad](uint16_t, void *ptr, uint16_t)
                   { std::memcpy(ptr, &bad, sizeof(SyncManager::Register)); }),
            Return(sizeof(SyncManager::Register))));
    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, pdo_.isConfigOk());
}

// ---- activateOutput() / activateInput() ----

TEST_F(PDOTest, activateOutput_unused_type_no_esc_calls)
{
    // Fresh PDO: the fixture configures in SetUp, so test the pre-configure state here.
    PDO unconfigured{&esc_};
    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    unconfigured.activateOutput(true);
}

TEST_F(PDOTest, activateInput_unused_type_no_esc_calls)
{
    PDO unconfigured{&esc_};
    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    unconfigured.activateInput(true);
}

TEST_F(PDOTest, activateOutput_true_calls_activateSm)
{
    configurePdo();
    EXPECT_CALL(esc_, write(smPdiAddr(2), _, sizeof(uint8_t))).Times(1);
    pdo_.activateOutput(true);
}

TEST_F(PDOTest, activateInput_true_calls_activateSm)
{
    configurePdo();
    EXPECT_CALL(esc_, write(smPdiAddr(3), _, sizeof(uint8_t))).Times(1);
    pdo_.activateInput(true);
}

TEST_F(PDOTest, activateOutput_false_calls_deactivateSm)
{
    configurePdo();
    setupDeactivatePdi(2);
    EXPECT_CALL(esc_, write(smPdiAddr(2), _, sizeof(uint8_t))).Times(1);
    pdo_.activateOutput(false);
}

TEST_F(PDOTest, activateInput_false_calls_deactivateSm)
{
    configurePdo();
    setupDeactivatePdi(3);
    EXPECT_CALL(esc_, write(smPdiAddr(3), _, sizeof(uint8_t))).Times(1);
    pdo_.activateInput(false);
}

// ---- updateInput() / updateOutput() ----

TEST_F(PDOTest, updateInput_null_buffer_no_write)
{
    PDO pdo_no_buf{&esc_};
    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    pdo_no_buf.updateInput();
}

TEST_F(PDOTest, updateOutput_null_buffer_no_read)
{
    PDO pdo_no_buf{&esc_};
    EXPECT_CALL(esc_, read(_, _, _)).Times(0);
    pdo_no_buf.updateOutput();
}

TEST_F(PDOTest, updateInput_writes_to_sm_address)
{
    configurePdo();
    EXPECT_CALL(esc_, write(PDO_IN_ADDR, static_cast<void const *>(input_), PDO_SIZE))
        .WillOnce(Return(PDO_SIZE));
    pdo_.updateInput();
}

TEST_F(PDOTest, updateOutput_reads_from_sm_address)
{
    configurePdo();
    EXPECT_CALL(esc_, read(PDO_OUT_ADDR, static_cast<void *>(output_), PDO_SIZE))
        .WillOnce(Return(PDO_SIZE));
    pdo_.updateOutput();
}

TEST_F(PDOTest, updateInput_write_error_does_not_crash)
{
    configurePdo();
    ON_CALL(esc_, write(PDO_IN_ADDR, _, _)).WillByDefault(Return(0));
    pdo_.updateInput();
}

TEST_F(PDOTest, updateOutput_read_error_does_not_crash)
{
    configurePdo();
    ON_CALL(esc_, read(PDO_OUT_ADDR, _, _)).WillByDefault(Return(0));
    pdo_.updateOutput();
}

// ---- configureMapping() ----

// Build a dictionary with optional TxPDO (input) and RxPDO (output) assignments.
// TxPDO: 0x1C13 → 0x1A00 → (0x6000, sub1=uint16_t 0x1234)
// RxPDO: 0x1C12 → 0x1600 → (0x7000, sub1=uint16_t 0x5678)
static CoE::Dictionary createMappingDict(bool with_input_assign, bool with_output_assign)
{
    CoE::Dictionary dict;

    {
        CoE::Object obj{0x6000, CoE::ObjectCode::VAR, "Input data", {}};
        CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                CoE::DataType::UNSIGNED16, "Val", uint16_t{0x1234});
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x7000, CoE::ObjectCode::VAR, "Output data", {}};
        CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                CoE::DataType::UNSIGNED16, "Val", uint16_t{0x5678});
        dict.push_back(std::move(obj));
    }

    {
        CoE::Object obj{0x1600, CoE::ObjectCode::RECORD, "RxPDO map", {}};
        CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x7000, 0, 16));
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
        CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x6000, 0, 16));
        dict.push_back(std::move(obj));
    }

    if (with_input_assign)
    {
        CoE::Object obj{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
        CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(obj, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
        dict.push_back(std::move(obj));
    }
    if (with_output_assign)
    {
        CoE::Object obj{0x1C12, CoE::ObjectCode::RECORD, "RxPDO assign", {}};
        CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(obj, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1600});
        dict.push_back(std::move(obj));
    }

    return dict;
}

TEST_F(PDOTest, configureMapping_no_assignments_returns_ok)
{
    CoE::Dictionary dict = createMappingDict(false, false);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_copies_sub_byte_default_into_buffer)
{
    // A one-bit default still needs one byte copied into the process image.
    CoE::Dictionary dict;
    {
        CoE::Object obj{0x6000, CoE::ObjectCode::VAR, "Input bit", {}};
        CoE::addEntry<uint8_t>(obj, 0, 1, 0, CoE::Access::READ | CoE::Access::WRITE,
                               CoE::DataType::BOOLEAN, "bit", uint8_t{1});
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0, CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{1});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x6000, 0, 1));
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0, CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(obj, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
        dict.push_back(std::move(obj));
    }

    setPdoLengths(1, PDO_SIZE);
    input_[0] = 0;
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
    ASSERT_EQ(input_[0], 1);
}

TEST_F(PDOTest, configureMapping_skips_padding_gap_entry)
{
    // Index 0 reserves bits without mapping an object.
    CoE::Dictionary dict;
    {
        CoE::Object obj{0x6000, CoE::ObjectCode::VAR, "Input", {}};
        CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                CoE::DataType::UNSIGNED16, "Val", uint16_t{0});
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0,  CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{2});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "pad",
                                makeMappingEntry(0, 0, 4));
        CoE::addEntry<uint32_t>(obj, 2, 32, 40, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x6000, 0, 16));
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0, CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(obj, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
        dict.push_back(std::move(obj));
    }
    setPdoLengths(3, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_input_aliases_entry_to_buffer)
{
    CoE::Dictionary dict = createMappingDict(true, false);
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
    ASSERT_EQ(0x1234, *static_cast<uint16_t *>(entry->data));
}

TEST_F(PDOTest, configureMapping_output_aliases_entry_to_buffer)
{
    CoE::Dictionary dict = createMappingDict(false, true);
    setPdoLengths(PDO_SIZE, 2);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x7000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(output_), entry->data);
    ASSERT_EQ(0x5678, *static_cast<uint16_t *>(entry->data));
}

TEST_F(PDOTest, configureMapping_both_assignments_succeed)
{
    CoE::Dictionary dict = createMappingDict(true, true);
    setPdoLengths(2, 2);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_input_size_differs_from_sm_returns_invalid_input)
{
    CoE::Dictionary dict = createMappingDict(true, false);
    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_output_size_differs_from_sm_returns_invalid_output)
{
    CoE::Dictionary dict = createMappingDict(false, true);
    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_rejected_mapping_leaves_dictionary_untouched)
{
    CoE::Dictionary dict = createMappingDict(true, false);
    {
        CoE::Object obj{0x6001, CoE::ObjectCode::VAR, "Other input", {}};
        CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                CoE::DataType::UNSIGNED16, "Val", uint16_t{0x4321});
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1A01, CoE::ObjectCode::RECORD, "TxPDO map 2", {}};
        CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x6001, 0, 16));
        dict.push_back(std::move(obj));
    }

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
    auto [obj0, rejected] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_FALSE(rejected->is_mapped);
    ASSERT_NE(static_cast<void *>(input_), rejected->data);
    ASSERT_EQ(0x1234, *static_cast<uint16_t *>(rejected->data));

    auto [assign, assigned_pdo] = CoE::findObject(dict, 0x1C13, 1);
    *static_cast<uint16_t *>(assigned_pdo->data) = 0x1A01;
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    ASSERT_FALSE(rejected->is_mapped);
    auto [obj1, accepted] = CoE::findObject(dict, 0x6001, 0);
    ASSERT_TRUE(accepted->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), accepted->data);
    ASSERT_EQ(0x4321, *static_cast<uint16_t *>(accepted->data));
}

namespace
{
    CoE::Dictionary createRemapDict()
    {
        CoE::Dictionary dict;
        for (auto [index, value] : {std::pair<uint16_t, uint16_t>{0x6000, 0x1234}, {0x6001, 0x4321}})
        {
            CoE::Object obj{index, CoE::ObjectCode::VAR, "Input", {}};
            CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE, CoE::DataType::UNSIGNED16, "Val", value);
            dict.push_back(std::move(obj));
        }
        auto addPdo = [&dict](uint16_t index, std::vector<uint16_t> const& objects)
        {
            CoE::Object obj{index, CoE::ObjectCode::RECORD, "TxPDO map", {}};
            CoE::addEntry<uint8_t>(obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", static_cast<uint8_t>(objects.size()));
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                CoE::addEntry<uint32_t>(obj, static_cast<uint8_t>(i + 1), 32, static_cast<uint16_t>(8 + 32 * i), CoE::Access::READ,
                                        CoE::DataType::UNSIGNED32, "M", makeMappingEntry(objects[i], 0, 16));
            }
            dict.push_back(std::move(obj));
        };
        addPdo(0x1A00, {0x6000});
        addPdo(0x1A01, {0x6001});
        addPdo(0x1A02, {0x6001, 0x6000});

        CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
        CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
        dict.push_back(std::move(assign));
        return dict;
    }

    void assignTxPdo(CoE::Dictionary& dict, uint16_t pdo)
    {
        auto [obj, entry] = CoE::findObject(dict, 0x1C13, 1);
        *static_cast<uint16_t *>(entry->data) = pdo;
    }

    uint16_t valueOf(CoE::Dictionary& dict, uint16_t index)
    {
        auto [obj, entry] = CoE::findObject(dict, index, 0);
        return *static_cast<uint16_t *>(entry->data);
    }
}

TEST_F(PDOTest, configureMapping_remap_releases_entries_of_previous_mapping)
{
    CoE::Dictionary dict = createRemapDict();
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
    auto [obj0, first] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_EQ(static_cast<void *>(input_), first->data);

    uint16_t written = 0xBEEF;
    std::memcpy(input_, &written, sizeof(written));

    assignTxPdo(dict, 0x1A01);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    ASSERT_FALSE(first->is_mapped);
    ASSERT_NE(static_cast<void *>(input_), first->data);
    ASSERT_EQ(0xBEEF, valueOf(dict, 0x6000));

    auto [obj1, second] = CoE::findObject(dict, 0x6001, 0);
    ASSERT_TRUE(second->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), second->data);
    ASSERT_EQ(0x4321, valueOf(dict, 0x6001));

    input_[0] = 0;
    ASSERT_EQ(0xBEEF, valueOf(dict, 0x6000));
}

TEST_F(PDOTest, configureMapping_remap_moves_a_kept_entry_without_corrupting_others)
{
    CoE::Dictionary dict = createRemapDict();
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));
    uint16_t written = 0x1111;
    std::memcpy(input_, &written, sizeof(written));

    assignTxPdo(dict, 0x1A02);
    setPdoLengths(4, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj0, moved] = CoE::findObject(dict, 0x6000, 0);
    auto [obj1, added] = CoE::findObject(dict, 0x6001, 0);
    ASSERT_EQ(static_cast<void *>(input_ + 2), moved->data);
    ASSERT_EQ(static_cast<void *>(input_),     added->data);
    ASSERT_EQ(0x1111, valueOf(dict, 0x6000));
    ASSERT_EQ(0x4321, valueOf(dict, 0x6001));
}

TEST_F(PDOTest, configureMapping_new_dictionary_releases_the_previous_one)
{
    CoE::Dictionary first = createRemapDict();
    CoE::Dictionary second = createRemapDict();
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(first));
    uint16_t written = 0xBEEF;
    std::memcpy(input_, &written, sizeof(written));

    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(second));

    auto [obj_old, old_entry] = CoE::findObject(first, 0x6000, 0);
    ASSERT_FALSE(old_entry->is_mapped);
    ASSERT_NE(static_cast<void *>(input_), old_entry->data);
    ASSERT_EQ(0xBEEF, valueOf(first, 0x6000));

    auto [obj_new, new_entry] = CoE::findObject(second, 0x6000, 0);
    ASSERT_EQ(static_cast<void *>(input_), new_entry->data);
    ASSERT_EQ(0x1234, valueOf(second, 0x6000));
}

TEST_F(PDOTest, configureMapping_rejected_output_leaves_inputs_unbound)
{
    CoE::Dictionary dict = createMappingDict(true, true);
    setPdoLengths(2, PDO_SIZE);
    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, pdo_.configureMapping(dict));

    auto [obj_in, input_entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_FALSE(input_entry->is_mapped);
    auto [obj_out, output_entry] = CoE::findObject(dict, 0x7000, 0);
    ASSERT_FALSE(output_entry->is_mapped);
}

TEST_F(PDOTest, configureMapping_empty_assignment_with_sm_returns_invalid_input)
{
    // The master requested SAFE_OP before assigning the PDOs.
    CoE::Dictionary dict = createMappingDict(false, false);
    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{0});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_input_pdo_map_missing_returns_invalid_input)
{
    CoE::Dictionary dict = createMappingDict(false, false);

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A99});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_output_pdo_map_missing_returns_invalid_output)
{
    CoE::Dictionary dict = createMappingDict(false, false);

    CoE::Object assign{0x1C12, CoE::ObjectCode::RECORD, "RxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1699});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_mapping_size_exceeds_buffer_returns_invalid)
{
    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    CoE::addEntry<uint8_t>(data_obj, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Val", uint8_t{0});
    dict.push_back(std::move(data_obj));

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 200));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_mapped_od_entry_not_found_returns_invalid)
{
    CoE::Dictionary dict;

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x9999, 1, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_missing_sub_entry_in_pdo_map_returns_invalid)
{
    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    CoE::addEntry<uint16_t>(data_obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                            CoE::DataType::UNSIGNED16, "Val", uint16_t{0});
    dict.push_back(std::move(data_obj));

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{2});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_already_mapped_entry_is_not_freed)
{
    // This entry points to storage it does not own; rebinding must not free it.
    uint16_t aliased_value = 0xABCD;

    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    data_obj.entries.emplace_back(0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                  CoE::DataType::UNSIGNED16, "Val");
    data_obj.entries[0].data = &aliased_value;
    data_obj.entries[0].is_mapped = true;
    dict.push_back(std::move(data_obj));
    setPdoLengths(2, PDO_SIZE);

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
}

TEST_F(PDOTest, configureMapping_null_old_data_no_memcpy)
{
    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    CoE::addEntry(data_obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                  CoE::DataType::UNSIGNED16, "Val", nullptr);
    dict.push_back(std::move(data_obj));
    setPdoLengths(2, PDO_SIZE);

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
}

TEST_F(PDOTest, configureMapping_mailboxless_input_on_sm0_uses_0x1C10)
{
    // A mailboxless input on SM0 uses assignment 0x1C10.
    SyncManager::Register sm_in_sm0 = makeSM(PDO_IN_ADDR, sizeof(uint16_t), SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
    setupSm(0, sm_in_sm0);
    setupSm(1, sm_empty_);
    setupSm(2, sm_empty_);
    setupSm(3, sm_empty_);
    setupSm(4, sm_empty_);
    ASSERT_EQ(0, pdo_.configure());

    CoE::Dictionary dict;
    {
        CoE::Object obj{0x6000, CoE::ObjectCode::VAR, "Input", {}};
        CoE::addEntry<uint16_t>(obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                                CoE::DataType::UNSIGNED16, "Val", uint16_t{0x1234});
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0, CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{1});
        CoE::addEntry<uint32_t>(obj, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                                makeMappingEntry(0x6000, 0, 16));
        dict.push_back(std::move(obj));
    }
    {
        CoE::Object obj{0x1C10, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
        CoE::addEntry<uint8_t> (obj, 0, 8,  0, CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Count", uint8_t{1});
        CoE::addEntry<uint16_t>(obj, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
        dict.push_back(std::move(obj));
    }

    ASSERT_EQ(StatusCode::ECAT_NO_ERROR, pdo_.configureMapping(dict));

    auto [obj2, entry2] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry2);
    ASSERT_TRUE(entry2->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry2->data);
    ASSERT_EQ(0x1234, *static_cast<uint16_t *>(entry2->data));
}
