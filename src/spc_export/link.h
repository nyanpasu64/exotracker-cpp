#pragma once
// A toy linker for SPC700 programs, only handling 16-bit little-endian relocations.

#include "util/enum_map.h"
#include "util/copy_move.h"

#include <gsl/span>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spc_export::link {

/// Each object defines up to one symbol.
enum class Symbol : uint8_t {
    Frames,
    Patterns,
    Channel0,
    Channel1,
    Channel2,
    Channel3,
    Channel4,
    Channel5,
    Channel6,
    Channel7,
    LoopBodies,
    SampleBank,
    COUNT,
};

// Relative addressing.

/// We don't need negative offsets for now.
using Offset = uint16_t;

/// An instruction to add the ARAM address of the symbol to the 16-bit relative pointer
/// at &Object::_data[position].
struct Relocation {
    /// Address in owning object, in bytes.
    Offset position;

    /// Symbol to add.
    Symbol symbol;

    // The offset relative to the symbol is not stored in the relocation, but in
    // Object::_data.
};

/// An object file containing data and relocations. For simplicity, it only exports one
/// symbol: its start address. Some objects don't define symbols and cannot be
/// referenced by other objects or itself.
///
/// An Object holds a variable-size vector of bytes; you push bytes to its end.
class Object {
    std::optional<Symbol> _maybe_symbol;
    // TODO possibly add support for multiple (local/global?) symbols per object
    // (https://gitlab.com/exotracker/exotracker-cpp/-/merge_requests/77#alternative-linker-designs)
    // to simplify referencing loop points in frame lists and pattern data.

    /// The contents of an object file, containing only relative offsets.
    /// Offsets are converted into absolute when loaded into Linker.
    std::vector<uint8_t> _data;

    /// A list of locations of relative offsets in _data, along with the symbols
    /// (including our own *_maybe_symbol) used to convert them to absolute addresses.
    std::vector<Relocation> _relocs;

    friend class Linker;

private:
    DEFAULT_COPY(Object)

public:
    DEFAULT_MOVE(Object)
    Object(std::optional<Symbol> maybe_symbol);
    Object(std::optional<Symbol> maybe_symbol, std::vector<uint8_t> data);

    std::vector<uint8_t> & data() {
        return _data;
    }

    /// Returns the current size of _data.
    size_t size() const {
        return _data.size();
    }

    /// Returns the current size of _data, modulo 64k. (Any Object larger than 64k
    /// will be rejected by Linker anyway.)
    ///
    /// Another option (not taken) is to return an error upon exceeding 64k.
    /// This requires removing mutable data(), and making every push_* method
    /// return an error code.
    Offset curr_pos() const {
        return (Offset) _data.size();
    }

    /// Appends a "relative pointer to a symbol" to the end of _data.
    void push_reloc(Symbol symbol, Offset symbol_relative = 0);

    void push_u8(uint8_t value);
    void push_u16(uint16_t value);
};

// Absolute addressing.
using Address = uint16_t;
using AramRef = gsl::span<uint8_t, 0x1'00'00>;

/// A Linker copies Objects' data into a fixed-size buffer representing ARAM, and
/// adjusts their relocations to properly point to themselves or other Objects (adding
/// the dependency's absolute address to the Object's relative offset).
///
/// Call Linker::add_object() to add an Object. If one Object depends on another
/// Object's symbol, you can add the first object after the dependency (Linker adjusts
/// the pointer immediately), or before it (Linker adjusts the pointer when the
/// dependency is added).
///
/// After adding all objects, call Linker::finalize() to check for objects referencing
/// missing objects (AKA pending relocations).
class Linker {
    AramRef _aram;

    // If this were an Address = u16, it couldn't distinguish the beginning and end of
    // _aram.
    size_t _current_address;

    EnumMap<Symbol, std::optional<Address>> _symbol_addresses;
    EnumMap<Symbol, std::vector<Address>> _pending_relocs;

public:
    Linker(AramRef aram, Address start_address)
        : _aram(aram)
        , _current_address(start_address)
    {}

    size_t current_address() const {
        return _current_address;
    }

    /// Align the current address to a multiple of 256 ($100).
    /// Used to position the sample directory.
    void align_address();

    // If needed, add set_address(Address).

    /// Add an object at current_address().
    /// If an error occurs (eg. memory overflows), returns an error message.
    [[nodiscard]] std::string add_object(Object const& obj);

    /// Returns a non-empty string if unresolved symbols are present.
    [[nodiscard]] std::string finalize();
};

} // namespace
