#include "ACPI.h"
#include "Common/String.h"
#include "Memory/MemoryManager.h"
#include "Memory/NonOwningVirtualRegion.h"

namespace kernel {

ACPI ACPI::s_instance;

void ACPI::initialize()
{
    if (!find_rsdp()) {
        log() << "ACPI: couldn't find RSDP";
        return;
    }

    if (!verify_checksum(m_rsdp, m_rsdp->revision == 2 ? rsdp_revision_2_size : rsdp_revision_1_size)) {
        warning() << "ACPI: RSDP invalid checksum detected";
        return;
    }

    log() << "ACPI: RSDP revision " << m_rsdp->revision << " @ " << MemoryManager::virtual_to_physical(m_rsdp) << " checksum OK";
    collect_all_sdts();
}

bool ACPI::find_rsdp()
{
    static constexpr StringView rsdp_signature = "RSD PTR "_sv;
    static constexpr size_t rsdp_alignment = 16;

    static constexpr Address ebda_base = 0x80000;
    auto ebda_range = Range::from_two_pointers(
        MemoryManager::physical_to_virtual(ebda_base),
        MemoryManager::physical_to_virtual(ebda_base + 1 * KB));

    auto rsdp = rsdp_signature.find_in_range(ebda_range, rsdp_alignment);

    if (rsdp) {
        m_rsdp = rsdp.as_pointer<RSDP>();
        return true;
    }

    static constexpr Address bios_base = 0xE0000;
    static constexpr Address bios_end = 0xFFFFF;
    auto bios_range = Range::from_two_pointers(
        MemoryManager::physical_to_virtual(bios_base),
        MemoryManager::physical_to_virtual(bios_end));

    rsdp = rsdp_signature.find_in_range(bios_range, rsdp_alignment);

    if (rsdp) {
        m_rsdp = rsdp.as_pointer<RSDP>();
        return true;
    }

    return false;
}

size_t ACPI::length_of_table_from_physical_address(Address physical)
{
    return TypedMapping<SDTHeader>::create("ACPI"_sv, physical)->length;
}

void ACPI::collect_all_sdts()
{
    Address root_sdt_physical;
    StringView root_sdt_name;
    size_t pointer_stride;

    if (m_rsdp->revision == 2) {
        if (m_rsdp->xsdt_pointer > MemoryManager::max_memory_address)
            FAILED_ASSERTION("ACPI: XSDT is outside of accessible range");

        root_sdt_physical = static_cast<Address::underlying_pointer_type>(m_rsdp->xsdt_pointer);
        root_sdt_name = "XSDT"_sv;
        pointer_stride = 8;
    } else {
        root_sdt_physical = static_cast<Address::underlying_pointer_type>(m_rsdp->rsdt_pointer);
        root_sdt_name = "RSDT"_sv;
        pointer_stride = 4;
    }

    auto root_sdt_length = length_of_table_from_physical_address(root_sdt_physical);
    auto root_sdt_mapping = TypedMapping<SDTHeader>::create("ACPI"_sv, root_sdt_physical, root_sdt_length);
    Address root_sdt = root_sdt_mapping.get();

    if (!verify_checksum(root_sdt, root_sdt_length)) {
        log() << root_sdt_name << " invalid checksum detected";
        return;
    }

    log() << "ACPI: " << root_sdt_name << " checksum OK";

    root_sdt += sizeof(SDTHeader);
    auto root_sdt_end = root_sdt + (root_sdt_length - sizeof(SDTHeader));
    m_tables.reserve((root_sdt_end - root_sdt) / pointer_stride);

    for (; root_sdt < root_sdt_end; root_sdt += pointer_stride) {
        Address table_address;

        if (pointer_stride == 4)
            table_address = Address(*root_sdt.as_pointer<u32>());
        else
            table_address = Address(*root_sdt.as_pointer<u64>());

        auto length_of_table = length_of_table_from_physical_address(table_address);
        auto this_table_mapping = TypedMapping<SDTHeader>::create("ACPI"_sv, table_address, length_of_table);
        auto* this_table = this_table_mapping.get();

        bool checksum_ok = verify_checksum(this_table, length_of_table);
        auto signature_string = StringView::from_char_array(this_table->signature);

        if (checksum_ok) {
            log() << "ACPI: table " << signature_string << " @ " << table_address << " checksum OK";
        } else {
            warning() << "ACPI: table " << signature_string << " @ " << table_address << " invalid checksum detected";
            continue;
        }

        m_tables.append({ table_address, length_of_table, String(signature_string) });
    }
}

bool ACPI::verify_checksum(Address virtual_address, size_t length)
{
    u8* bytes = virtual_address.as_pointer<u8>();
    u8 sum = 0;

    for (size_t i = 0; i < length; ++i)
        sum += bytes[i];

    return sum == 0;
}

ACPI::TableInfo* ACPI::get_table_info(StringView signature)
{
    auto it = linear_search_for(m_tables.begin(), m_tables.end(),
        [&signature](const TableInfo& ti) {
            return ti.name == signature;
        });

    return it == m_tables.end() ? nullptr : &*it;
}

SMPData* ACPI::generate_smp_data()
{
    auto* madt_info = get_table_info("APIC"_sv);

    if (!madt_info)
        return nullptr;

    auto madt_mapping = TypedMapping<MADT>::create("MADT"_sv, madt_info->physical_address, madt_info->length);
    Address madt = madt_mapping.get();
    auto* smp_info = new SMPData();

    smp_info->lapic_address = madt_mapping->lapic_address;
    log() << "ACPI: LAPIC @ " << smp_info->lapic_address;

    madt += sizeof(MADT);
    auto madt_end = madt + (madt_info->length - sizeof(MADT));

    smp_info->bsp_lapic_id = (CPU::ID(0x1).b & 0xFF000000) >> 24;

    for (u32 i = 0; i < legacy_irq_count; ++i) {
        smp_info->legacy_irqs_to_info[i] = { i, i, default_polarity_for_bus(Bus::ISA), default_trigger_mode_for_bus(Bus::ISA) };
    }

    DynamicArray<Pair<u8, LAPICInfo::NMI>> lapic_uid_to_nmi;

    while (madt < madt_end) {
        auto* entry = madt.as_pointer<MADT::EntryHeader>();

        switch (entry->type) {
        case MADT::EntryType::LAPIC: {
            auto* lapic = madt.as_pointer<MADT::LAPIC>();
            auto is_bsp = lapic->id == smp_info->bsp_lapic_id;

            log() << "ACPI: CPU " << lapic->id << " is bsp: " << is_bsp << " | online capable: " << !lapic->should_be_ignored();

            if (lapic->should_be_ignored())
                break;

            smp_info->lapics.append({ lapic->id, lapic->acpi_uid, {} });
            break;
        }
        case MADT::EntryType::IOAPIC: {
            auto* ioapic = madt.as_pointer<MADT::IOAPIC>();

            log() << "ACPI: IOAPIC " << ioapic->id << " @ " << format::as_hex << ioapic->address
                  << " gsi base " << format::as_dec << ioapic->gsi_base;

            smp_info->ioapics.append({ ioapic->id, { ioapic->gsi_base, 0 }, ioapic->address });

            break;
        }
        case MADT::EntryType::INTERRUPT_SOURCE_OVERRIDE: {
            auto* iso = madt.as_pointer<MADT::InterruptSourceOverride>();

            // Spec says this is the bus that gets overriden
            ASSERT(iso->bus == Bus::ISA);

            log() << "ACPI: interrupt override " << iso->source << " -> " << iso->gsi
                  << " polarity: " << to_string(iso->polarity) << " | trigger mode: " << to_string(iso->trigger_mode);

            auto polarity = iso->polarity == Polarity::CONFORMING ? default_polarity_for_bus(Bus::ISA) : iso->polarity;
            auto trigger_mode = iso->trigger_mode == TriggerMode::CONFORMING ? default_trigger_mode_for_bus(Bus::ISA) : iso->trigger_mode;

            smp_info->legacy_irqs_to_info[iso->source] = { iso->source, iso->gsi, polarity, trigger_mode };

            break;
        }
        case MADT::EntryType::LAPIC_ADDRESS_OVERRIDE: {
            auto* lapic_override = madt.as_pointer<MADT::LAPICAddressOverride>();

            // Some protection for the 32 bit kernel
            if (lapic_override->address > MemoryManager::max_memory_address)
                FAILED_ASSERTION("LAPIC override address is outside of accessible range");

            smp_info->lapic_address = static_cast<Address::underlying_pointer_type>(lapic_override->address);

            log() << "ACPI: Overriding LAPIC address to " << smp_info->lapic_address;

            break;
        }
        case MADT::EntryType::NMI_SOURCE: {
            auto nmi_source = madt.as_pointer<MADT::NMISource>();

            log() << "ACPI: IOAPIC nmi source at gsi " << nmi_source->gsi;

            smp_info->ioapic_nmis.append({ nmi_source->polarity, nmi_source->trigger_mode, nmi_source->gsi });

            break;
        }
        case MADT::EntryType::LAPIC_NMI: {
            auto lapic_nmi = madt.as_pointer<MADT::LAPICNMI>();
            log() << "ACPI: lapic " << lapic_nmi->acpi_uid
                  << " nmi at lint " << lapic_nmi->lint_number
                  << " polarity: " << to_string(lapic_nmi->polarity)
                  << " | trigger mode: " << to_string(lapic_nmi->trigger_mode);

            Pair<u8, LAPICInfo::NMI> nmi {};
            nmi.first = lapic_nmi->acpi_uid;

            auto& info = nmi.second;
            info.lint = lapic_nmi->lint_number;
            info.polarity = lapic_nmi->polarity == Polarity::CONFORMING ? Polarity::ACTIVE_HIGH : lapic_nmi->polarity;
            info.trigger_mode = lapic_nmi->trigger_mode == TriggerMode::CONFORMING ? TriggerMode::EDGE : lapic_nmi->trigger_mode;

            lapic_uid_to_nmi.append(nmi);

            break;
        }
        default:
            break;
        }

        madt += entry->length;
    }

    static constexpr u8 all_processors_uid = 0xFF;

    if (!lapic_uid_to_nmi.empty()) {
        if (lapic_uid_to_nmi[0].first == all_processors_uid) { // single NMI entry for all LAPICs
            ASSERT(lapic_uid_to_nmi.size() == 1);

            for (auto& lapic : smp_info->lapics)
                lapic.nmi_connection = lapic_uid_to_nmi[0].second;
        } else {
            for (auto& nmi_entry : lapic_uid_to_nmi) {
                auto& lapics = smp_info->lapics;

                auto uid = nmi_entry.first;

                auto lapic = linear_search_for(lapics.begin(), lapics.end(),
                    [uid](const LAPICInfo& info) {
                        return info.acpi_uid == uid;
                    });

                // You may think that this is an error, but it's actually not.
                // One of my laptops generates 8 LAPIC NMIs even though theres 4 LAPICs
                if (lapic == lapics.end())
                    continue;

                lapic->nmi_connection = nmi_entry.second;
            }
        }
    }

    return smp_info;
}

}
