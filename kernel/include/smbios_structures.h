#include <common/types.h>
#include <common/attributes.h>
#include <common/byte_order.h>

#define DMI_SIGNATURE "_DMI_"
#define SMBIOS2_SIGNATURE "_SM_"
#define SMBIOS3_SIGNATURE "_SM3_"

struct PACKED dmi_entrypoint {
    char signature[5];
    u8 checksum;
    le16 table_length;
    le32 table_address;
    le16 num_items;
    u8 revision;
};
EXPECT_SIZEOF(struct dmi_entrypoint, 0xF);

struct PACKED smbios2_entrypoint {
    char signature[4];
    u8 checksum;
    u8 size;
    u8 major;
    u8 minor;
    le16 max_structure_size;
    u8 revision;
    u8 formatted_area[5];
    struct dmi_entrypoint dmi_entryoint;
};
EXPECT_SIZEOF(struct smbios2_entrypoint, 0x1F);

struct PACKED smbios3_entrypoint {
    char signature[5];
    u8 checksum;
    u8 size;
    u8 major;
    u8 minor;
    u8 docrev;
    u8 revision;
    u8 rsvd;
    le32 max_structure_size;
    le64 table_address;
};
EXPECT_SIZEOF(struct smbios3_entrypoint, 0x18);

// smbios_structure_hdr->type
enum smbios_structure_type {
    SMBIOS_STRUCTURE_TYPE_BIOS_INFORMATION = 0,
    SMBIOS_STRUCTURE_TYPE_SYSTEM_INFORMATION = 1,
    SMBIOS_STRUCTURE_TYPE_BOARD_INFORMATION = 2,
    SMBIOS_STRUCTURE_TYPE_CHASSIS_INFORMATION = 3,
    SMBIOS_STRUCTURE_TYPE_PROCESSOR_INFORMATION = 4,
    SMBIOS_STRUCTURE_TYPE_CACHE_INFORMATION = 7,
    SMBIOS_STRUCTURE_TYPE_END_OF_TABLE = 127,
};

struct PACKED smbios_structure_hdr {
    u8 type;
    u8 size;
    le16 handle;
};
EXPECT_SIZEOF(struct smbios_structure_hdr, 0x04);

struct smbios_string_value {
    u8 idx_plus_one;
};

struct PACKED smbios_bios_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value vendor;
    struct smbios_string_value version;
    le16 bios_segment;
    struct smbios_string_value release_date;
    u8 rom_size;
    le64 bios_characteristics;
    u8 bios_characteristics_extension[2];
    u8 bios_major_release;
    u8 bios_minor_release;
    u8 ec_firmware_major_release;
    u8 ec_firmware_minor_release;
    le16 extended_rom_size;
};
EXPECT_SIZEOF(struct smbios_bios_information, 0x1A);

struct PACKED smbios_system_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value manufacturer;
    struct smbios_string_value product_name;
    struct smbios_string_value version;
    struct smbios_string_value serial_number;
    u8 uuid[16];
    u8 wake_up_type;
    struct smbios_string_value sku_number;
    struct smbios_string_value family;
};
EXPECT_SIZEOF(struct smbios_system_information, 0x1B);

struct PACKED smbios_board_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value manufacturer;
    struct smbios_string_value product;
    struct smbios_string_value version;
    struct smbios_string_value serial_number;
    struct smbios_string_value asset_tag;
    u8 feature_flag;
    struct smbios_string_value location_in_chassis;
    le16 chassis_handle;
    u8 board_type;
    u8 number_of_contained_objects;
    le16 contained_objects[];
};
EXPECT_SIZEOF(struct smbios_board_information, 0x0F);

struct PACKED smbios_chassis_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value manufacturer;
    u8 type;
    struct smbios_string_value version;
    struct smbios_string_value serial_number;
    struct smbios_string_value asset_tag;
    u8 bootup_state;
    u8 power_supply_state;
    u8 thermal_state;
    u8 security_status;
    le32 oem_defined;
    u8 height;
    u8 number_of_power_cords;
    u8 number_of_contained_elements;
    u8 contained_element_record_length;
    /*
     * Variable length elements follow:
     * u8 arr[number_of_contained_elements * contained_element_record_length];
     * struct smbios_string_value sku_number;
     */
};
EXPECT_SIZEOF(struct smbios_chassis_information, 0x15);

struct PACKED smbios_processor_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value socket_designation;
    u8 processor_type;
    u8 processor_family;
    struct smbios_string_value manufacturer;
    u64 processor_id;
    struct smbios_string_value version;
    u8 voltage;
    le16 external_clock;
    le16 max_speed;
    le16 current_speed;
    u8 status;
    u8 processor_upgrade;
    le16 l1_cache_handle;
    le16 l2_cache_handle;
    le16 l3_cache_handle;
    struct smbios_string_value serial_number;
    struct smbios_string_value asset_tag;
    struct smbios_string_value part_number;
    u8 core_count;
    u8 core_enabled;
    u8 thread_count;
    le16 processor_characteristics;
    le16 processor_family_2;
    le16 core_count_2;
    le16 core_enabled_2;
    le16 thread_count_2;
    le16 thread_enabled;
};
EXPECT_SIZEOF(struct smbios_processor_information, 0x32);

struct PACKED smbios_cache_information {
    struct smbios_structure_hdr hdr;
    struct smbios_string_value socket_designation;
    le16 cache_configuration;
    le16 maximum_cache_size;
    le16 installed_size;
    le16 supported_sram_type;
    le16 current_sram_type;
    u8 cache_speed;
    u8 error_correction_type;
    u8 system_cache_type;
    u8 associativity;
    le32 maximum_cache_size_2;
    le32 installed_size_2;
};
EXPECT_SIZEOF(struct smbios_cache_information, 0x1B);
