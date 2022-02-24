#include "link.h"
#include "util/release_assert.h"

#include <fmt/core.h>
#include <stdexcept>

namespace spc_export::link {

/// Cast to register width and force unsigned full-width arithmetic.
#define REG(x)  (size_t) (x)

static inline uint16_t get_u16(gsl::span<const uint8_t> data, size_t addr) {
    // Bounds-check. Ignore integer overflow.
    if (addr + 2 > data.size()) {
        throw std::out_of_range(fmt::format("get_u16({}) out of range", addr));
    }

    return (uint16_t) (REG(data[addr]) | REG(data[addr + 1]) << 8);
}

static inline void set_u16(gsl::span<uint8_t> data, size_t addr, uint16_t value) {
    // Bounds-check. Ignore integer overflow.
    if (addr + 2 > data.size()) {
        throw std::out_of_range(fmt::format(
            "set_u16({} = {}) out of range", addr, value
        ));
    }

    data[addr] = (uint8_t) REG(value);
    data[addr + 1] = (uint8_t) (REG(value) >> 8);
}


Object::Object(std::optional<Symbol> maybe_symbol)
    : _maybe_symbol(maybe_symbol)
{}

Object::Object(std::optional<Symbol> maybe_symbol, std::vector<uint8_t> data)
    : _maybe_symbol(maybe_symbol)
    , _data(std::move(data))
{}

void Object::push_reloc(Symbol symbol, Offset symbol_relative) {
    auto size = this->size();
    auto curr_pos = (Offset) size;

    // If this Object is over 64 kilobytes, don't push a relocation.
    // What we do doesn't really matter, since any such object is invalid
    // and will be rejected by the linker.
    if (curr_pos == size) {
        _relocs.push_back(Relocation {
            .position = curr_pos,
            .symbol = symbol,
        });
    }

    push_u16(symbol_relative);
}

void Object::push_u8(uint8_t value) {
    _data.push_back(value);
}

void Object::push_u16(uint16_t value) {
    _data.push_back(uint8_t(value & 0xFF));
    _data.push_back(uint8_t(value >> 8));
}

void Linker::align_address() {
    _current_address = (_current_address + 0xFF) & ~(size_t) 0xFF;
}

std::string Linker::add_object(Object const& obj) {
    // We don't currently validate that objects don't overlap.

    size_t obj_begin = _current_address;
    size_t obj_size = obj._data.size();
    size_t obj_end = obj_begin + obj_size;

    auto symbol_name = [](Object const& obj) -> std::string {
        if (auto symbol = obj._maybe_symbol) {
            return fmt::format("symbol {}", (size_t) *symbol);
        } else {
            return "Unknown";
        }
    };

    // obj's span is OOB if its end exceeds the outer span's end.
    if (obj_end > _aram.size()) {
        return fmt::format(
            "ARAM overflow, writing {} with size {:#x} to address {:#x}",
            symbol_name(obj), obj._data.size(), obj_begin);
    }

    if (auto symbol = obj._maybe_symbol) {
        // Verify we don't insert two objects with the same symbol.
        // This doesn't detect two equivalent objects both without a symbol.
        if (_symbol_addresses[*symbol]) {
            return fmt::format(
                "Cannot insert two objects defining symbol {}", (size_t) *symbol
            );
        }

        // Insert the object's symbol if present.
        _symbol_addresses[*symbol] = (Address) _current_address;

        // Relocate other objects' references to this object's symbol.
        for (Address reloc_addr : _pending_relocs[*symbol]) {
            // A bounds check is unnecessary because all reloc_addr pushed to
            // _pending_relocs[] are bounds-checked.
            auto value = get_u16(_aram, reloc_addr);
            value += (Address) obj_begin;
            set_u16(_aram, reloc_addr, value);
        }
        _pending_relocs[*symbol].clear();
    }

    // Write the object data to ARAM.
    std::copy(obj._data.cbegin(), obj._data.cend(), _aram.data() + obj_begin);

    // Process relocations within this object. Note that obj may be self-referential.
    for (Relocation reloc : obj._relocs) {
        size_t reloc_addr = obj_begin + reloc.position;
        if (reloc_addr + 2 > obj_end) {
            return fmt::format(
                "Invalid relocation in {}, offset {:#x} OOB in size {:#x}",
                symbol_name(obj), reloc.position, obj_size);
        }

        if (auto symbol_addr = _symbol_addresses[reloc.symbol]) {
            // Relocate this object's reference to the symbol.
            auto value = get_u16(_aram, reloc_addr);
            value += *symbol_addr;
            set_u16(_aram, reloc_addr, value);
        } else {
            _pending_relocs[reloc.symbol].push_back((Address) reloc_addr);
        }
    }

    _current_address = obj_end;
    return "";
}

std::string Linker::finalize() {
    std::string err;
    for (size_t i = 0; i < enum_count<Symbol>; i++) {
        if (!_pending_relocs[i].empty()) {
            if (err.empty()) {
                err.append("Unresolved symbols: ");
            } else {
                err.append(", ");
            }
            fmt::format_to(std::back_inserter(err), "{}", i);
        }
    }
    return err;
}

} // namespace
