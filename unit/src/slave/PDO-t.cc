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

static SyncManager makeSM(uint16_t start, uint16_t length, uint8_t control)
{
    SyncManager sm{};
    sm.start_address = start;
    sm.length = length;
    sm.control = control;
    sm.activate = SM_ACTIVATE_ENABLE;
    return sm;
}

static constexpr uint16_t smPdiAddr(uint8_t index)
{
    return static_cast<uint16_t>(reg::SYNC_MANAGER + index * sizeof(SyncManager) + 7);
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

    SyncManager sm_pdo_in_ = makeSM(PDO_IN_ADDR, PDO_SIZE, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ);
    SyncManager sm_pdo_out_ = makeSM(PDO_OUT_ADDR, PDO_SIZE, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE);
    SyncManager sm_mbx_in_ = makeSM(0x1000, 256, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ);
    SyncManager sm_mbx_out_ = makeSM(0x1200, 256, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE);
    SyncManager sm_empty_{};

    void SetUp() override
    {
        pdo_.setInput(input_, PDO_SIZE);
        pdo_.setOutput(output_, PDO_SIZE);
        setupSmReads();
    }

    void setupSmReads()
    {
        auto setupSm = [this](int idx, SyncManager const &sm)
        {
            ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager) * idx), _, sizeof(SyncManager)))
                .WillByDefault(DoAll(
                    Invoke([sm](uint16_t, void *ptr, uint16_t)
                           { std::memcpy(ptr, &sm, sizeof(SyncManager)); }),
                    Return(sizeof(SyncManager))));
        };

        setupSm(0, sm_mbx_in_);
        setupSm(1, sm_mbx_out_);
        setupSm(2, sm_pdo_in_);
        setupSm(3, sm_pdo_out_);
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

TEST_F(PDOTest, configure_failure_no_buffered_sm)
{
    // Replace all SMs with mailbox-only (control=0x02), so findSm(BUFFERED) throws
    SyncManager mbx{};
    mbx.control = SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ;
    for (int i = 0; i < 5; ++i)
    {
        ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager) * i), _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([mbx](uint16_t, void *ptr, uint16_t)
                       { std::memcpy(ptr, &mbx, sizeof(SyncManager)); }),
                Return(sizeof(SyncManager))));
    }
    ASSERT_EQ(-EINVAL, pdo_.configure());
}

// ---- isConfigOk() ----

TEST_F(PDOTest, isConfigOk_no_error)
{
    configurePdo();
    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.isConfigOk());
}

TEST_F(PDOTest, isConfigOk_invalid_input_length_mismatch)
{
    configurePdo();
    SyncManager bad = sm_pdo_in_;
    bad.length = PDO_SIZE + 1;
    ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager) * 2), _, sizeof(SyncManager)))
        .WillByDefault(DoAll(
            Invoke([bad](uint16_t, void *ptr, uint16_t)
                   { std::memcpy(ptr, &bad, sizeof(SyncManager)); }),
            Return(sizeof(SyncManager))));
    ASSERT_EQ(StatusCode::INVALID_INPUT_CONFIGURATION, pdo_.isConfigOk());
}

TEST_F(PDOTest, isConfigOk_invalid_output_length_mismatch)
{
    configurePdo();
    SyncManager bad = sm_pdo_out_;
    bad.length = PDO_SIZE + 1;
    ON_CALL(esc_, read(static_cast<uint16_t>(reg::SYNC_MANAGER + sizeof(SyncManager) * 3), _, sizeof(SyncManager)))
        .WillByDefault(DoAll(
            Invoke([bad](uint16_t, void *ptr, uint16_t)
                   { std::memcpy(ptr, &bad, sizeof(SyncManager)); }),
            Return(sizeof(SyncManager))));
    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, pdo_.isConfigOk());
}

// ---- activateOutput() / activateInput() ----

TEST_F(PDOTest, activateOutput_unused_type_no_esc_calls)
{
    // Before configure(), sm_output_.type == SyncManagerType::Unused
    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    pdo_.activateOutput(true);
}

TEST_F(PDOTest, activateInput_unused_type_no_esc_calls)
{
    EXPECT_CALL(esc_, write(_, _, _)).Times(0);
    pdo_.activateInput(true);
}

TEST_F(PDOTest, activateOutput_true_calls_activateSm)
{
    configurePdo();
    EXPECT_CALL(esc_, write(smPdiAddr(3), _, sizeof(uint8_t))).Times(1);
    pdo_.activateOutput(true);
}

TEST_F(PDOTest, activateInput_true_calls_activateSm)
{
    configurePdo();
    EXPECT_CALL(esc_, write(smPdiAddr(2), _, sizeof(uint8_t))).Times(1);
    pdo_.activateInput(true);
}

TEST_F(PDOTest, activateOutput_false_calls_deactivateSm)
{
    configurePdo();
    setupDeactivatePdi(3);
    EXPECT_CALL(esc_, write(smPdiAddr(3), _, sizeof(uint8_t))).Times(1);
    pdo_.activateOutput(false);
}

TEST_F(PDOTest, activateInput_false_calls_deactivateSm)
{
    configurePdo();
    setupDeactivatePdi(2);
    EXPECT_CALL(esc_, write(smPdiAddr(2), _, sizeof(uint8_t))).Times(1);
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

    // Actual application data objects
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

    // PDO mapping objects
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
    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_input_aliases_entry_to_buffer)
{
    CoE::Dictionary dict = createMappingDict(true, false);
    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
    ASSERT_EQ(0x1234, *static_cast<uint16_t *>(entry->data));
}

TEST_F(PDOTest, configureMapping_output_aliases_entry_to_buffer)
{
    CoE::Dictionary dict = createMappingDict(false, true);
    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x7000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(output_), entry->data);
    ASSERT_EQ(0x5678, *static_cast<uint16_t *>(entry->data));
}

TEST_F(PDOTest, configureMapping_both_assignments_succeed)
{
    CoE::Dictionary dict = createMappingDict(true, true);
    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));
}

TEST_F(PDOTest, configureMapping_input_pdo_map_missing_returns_invalid_input)
{
    CoE::Dictionary dict = createMappingDict(false, false);

    // Assignment references a PDO map that doesn't exist in the dict
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

    // Single mapping entry with 200 bits → 25 bytes > PDO_SIZE (16 bytes)
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

    // PDO mapping references index 0x9999 which doesn't exist
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

    // Count = 2 but only sub[1] exists, sub[2] is missing
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
    // Simulate an entry already aliased (is_mapped=true). It should be re-aliased
    // without calling std::free on its data pointer.
    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    CoE::addEntry<uint16_t>(data_obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                            CoE::DataType::UNSIGNED16, "Val", uint16_t{0xABCD});
    // Mark as already aliased — data must not be freed by parsePdoMap
    data_obj.entries[0].is_mapped = true;
    dict.push_back(std::move(data_obj));

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
}

TEST_F(PDOTest, configureMapping_null_old_data_no_memcpy)
{
    // Entry with data=nullptr — parsePdoMap should skip the memcpy
    CoE::Dictionary dict;

    CoE::Object data_obj{0x6000, CoE::ObjectCode::VAR, "Data", {}};
    CoE::addEntry(data_obj, 0, 16, 0, CoE::Access::READ | CoE::Access::WRITE,
                  CoE::DataType::UNSIGNED16, "Val", nullptr);
    dict.push_back(std::move(data_obj));

    CoE::Object pdo_map{0x1A00, CoE::ObjectCode::RECORD, "TxPDO map", {}};
    CoE::addEntry<uint8_t>(pdo_map, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint32_t>(pdo_map, 1, 32, 8, CoE::Access::READ, CoE::DataType::UNSIGNED32, "M1",
                            makeMappingEntry(0x6000, 0, 16));
    dict.push_back(std::move(pdo_map));

    CoE::Object assign{0x1C13, CoE::ObjectCode::RECORD, "TxPDO assign", {}};
    CoE::addEntry<uint8_t>(assign, 0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Count", uint8_t{1});
    CoE::addEntry<uint16_t>(assign, 1, 16, 8, CoE::Access::READ, CoE::DataType::UNSIGNED16, "PDO 1", uint16_t{0x1A00});
    dict.push_back(std::move(assign));

    ASSERT_EQ(StatusCode::NO_ERROR, pdo_.configureMapping(dict));

    auto [obj, entry] = CoE::findObject(dict, 0x6000, 0);
    ASSERT_NE(nullptr, entry);
    ASSERT_TRUE(entry->is_mapped);
    ASSERT_EQ(static_cast<void *>(input_), entry->data);
}
