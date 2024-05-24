#include "cpu/pred/whisper.hh"

#include <type_traits>

#include "debug/Whisper.hh"

namespace gem5::branch_prediction
{

namespace
{
/**
 * Converts an unsigned number to a \c std::bitset.
 *
 * @tparam N Number of bits in the resulting \c std::bitset.
 * @tparam SrcT Source unsigned integer type.
 * @param src Source value.
 * @return \c std::bitset representing \c src.
 */
template<std::size_t N, typename SrcT,
         typename = std::enable_if_t<std::is_unsigned_v<SrcT>>>
std::bitset<N> to_bitset(const SrcT& src) noexcept
{
    return std::bitset<N>{src & mask(N)};
}

/**
 * Implementation for a single Read-Once Monotone Boolean Formula unit.
 *
 * @param o Boolean formula selector.
 * @param b Branch history.
 * @return Prediction result.
 */
bool ROMBFSingleUnit(std::bitset<2> o, std::bitset<2> b)
{
    // 00 -> b1 && b0
    // 01 -> b1 || b0
    // 10 -> b1 || !b0
    // 11 -> b1 && !b0

    auto b0i = o[1] ? !b[0] : b[0];
    return (o[1] ^ o[0]) ? (b[1] || b0i) : (b[1] && b0i);
}

/**
 * Implementation for a full Read-Once Monotone Boolean Formula unit.
 *
 * @param o Boolean formula selector.
 * @param b Branch history.
 * @return Prediction result.
 */
bool ROMBFUnit(std::bitset<15> o, std::bitset<8> b)
{
    auto o10 = std::bitset<2>{o.to_ulong() & mask(2)};
    auto b10 = std::bitset<2>{b.to_ulong() & mask(2)};
    auto u0 = ROMBFSingleUnit(o10, b10);

    auto o54 = std::bitset<2>{(o.to_ulong() >> 4) & mask(2)};
    auto b32 = std::bitset<2>{(b.to_ulong() >> 2) & mask(2)};
    auto u1 = ROMBFSingleUnit(o54, b32);

    auto o32 = std::bitset<2>{(o.to_ulong() >> 2) & mask(2)};
    auto u10 = std::bitset<2>{((u1 << 1) | u0) & mask(2)};
    auto u2 = ROMBFSingleUnit(o32, u10);

    auto o98 = std::bitset<2>{(o.to_ulong() >> 8) & mask(2)};
    auto b54 = std::bitset<2>{(b.to_ulong() >> 4) & mask(2)};
    auto u3 = ROMBFSingleUnit(o98, b54);

    auto o1312 = std::bitset<2>{(o.to_ulong() >> 12) & mask(2)};
    auto b76 = std::bitset<2>{(b.to_ulong() >> 6) & mask(2)};
    auto u4 = ROMBFSingleUnit(o1312, b76);

    auto o1110 = std::bitset<2>{(o.to_ulong() >> 10) & mask(2)};
    auto u43 = std::bitset<2>{((u4 << 1) | u3) & mask(2)};
    auto u5 = ROMBFSingleUnit(o1110, u43);

    auto o76 = std::bitset<2>{(o.to_ulong() >> 6) & mask(2)};
    auto u52 = std::bitset<2>{((u5 << 1) | u2) & mask(2)};
    auto u6 = ROMBFSingleUnit(o76, u52);

    return o[15] ? u6 : !u6;
}
}  // namespace

WhisperBP::WhisperBP(const WhisperBPParams &params)
    : BPredUnit(params),
      hintBufferSize{params.hint_buffer_size},
      hintBuffer{},
      globalHistory{},
      fallbackPredictor{params.fallback_predictor}
{
    DPRINTF(Whisper, "Using Whisper branch predictor\n");
}

void WhisperBP::updateHistories(ThreadID tid, Addr pc, bool uncond,
                                bool taken, Addr target,
                                void *&bp_history)
{
    // Only update global history on conditional branches
    if (!uncond)
    {
        updateGlobalHistory(tid, taken);
    }

    auto hint_it = lookupBuffer(pc);
    if (hint_it == hintBuffer.end())
    {
        fallbackPredictor->updateHistories(tid, pc, uncond, taken, target,
                                           bp_history);
    }
}

bool WhisperBP::lookup(ThreadID tid, Addr pc, void *&bp_history)
{
    auto hint_pred = predict(tid, pc, true);

    if (hint_pred)
    {
        return *hint_pred;
    }
    else
    {
        return fallbackPredictor->lookup(tid, pc, bp_history);
    }
}

void WhisperBP::update(ThreadID tid, Addr pc, bool taken,
                       void *&bp_history, bool squashed,
                       const StaticInstPtr &inst, Addr target)
{
    if (!squashed)
    {
        auto hint_pred = predict(tid, pc, false);
        if (hint_pred)
        {
            DPRINTF(Whisper, "pc: %#0.10x -> Predicted: %s/Taken: %s [%s]\n",
                  pc, *hint_pred, taken,
                  (*hint_pred == taken) ? "GOOD" : "BAD");
        }
    }

    auto hint_it = lookupBuffer(pc);
    if (hint_it == hintBuffer.end())
    {
        fallbackPredictor->update(tid, pc, taken, bp_history, squashed, inst,
                                  target);
    }
}

void WhisperBP::squash(ThreadID tid, void *&bp_history)
{
    // We do not require a custom branch prediction history object. Rather, we
    // rely on thread-local global history.
    if (bp_history != nullptr)
    {
        fallbackPredictor->squash(tid, bp_history);
    }
}

void WhisperBP::insert(Addr pc, uint32_t hint)
{
    // Ensure that the hint buffer size does not exceed max size
    while (hintBuffer.size() >= hintBufferSize)
    {
        hintBuffer.pop_front();
    }

    auto hint_obj = Hint::fromUInt(hint);
    auto brPC = pc + hint_obj.pc_offset;
    hintBuffer.emplace_back(HintBufferEntry{brPC, hint});

    DPRINTF(Whisper,
            "Inserted entry {"
            "pc: %#0.10x, hint: %#0.10x "
            "{hist: %#0.3x, bool_formula: %#0.6x, bias: %#0.3x, pc: %#0.5x}"
            "}\n",
            brPC,
            hint,
            hint_obj.history,
            hint_obj.bool_formula,
            hint_obj.bias,
            hint_obj.pc_offset);
}

WhisperBP::Hint WhisperBP::Hint::fromUInt(uint32_t hint)
{
    return WhisperBP::Hint{
        static_cast<uint8_t>((hint >> 28) & mask(4)),
        static_cast<uint16_t>((hint >> 14) & mask(15)),
        static_cast<uint8_t>((hint >> 12) & mask(2)),
        static_cast<uint16_t>(hint & mask(12)),
    };
}

std::bitset<4> WhisperBP::Hint::history_bits() const
{
    return to_bitset<4>(history);
}

std::bitset<15> WhisperBP::Hint::bool_formula_bits() const
{
    return to_bitset<15>(bool_formula);
}

std::bitset<2> WhisperBP::Hint::bias_bits() const
{
    return to_bitset<2>(bias);
}

std::bitset<12> WhisperBP::Hint::pc_offset_bits() const
{
    return to_bitset<12>(pc_offset);
}

unsigned WhisperBP::Hint::histLength() const
{
    // Geometric Series: 8 * 1.3819 ^ hist
    switch (history)
    {
      case 0:
        return 8;
      case 1:
        return 11;
      case 2:
        return 15;
      case 3:
        return 21;
      case 4:
        return 29;
      case 5:
        return 40;
      case 6:
        return 56;
      case 7:
        return 77;
      case 8:
        return 106;
      case 9:
        return 147;
      case 10:
        return 203;
      case 11:
        return 281;
      case 12:
        return 388;
      case 13:
        return 536;
      case 14:
        return 741;
      case 15:
        return 1024;
      default:
        assert(false);
    }

    return 0;
}

std::optional<bool>
WhisperBP::predict(ThreadID tid, Addr pc, bool dprinf_pred)
{
    auto hint_it = lookupBuffer(pc);
    markUsed(hint_it);
    if (hint_it != hintBuffer.end())
    {
        Hint hint = Hint::fromUInt(hint_it->hint);

        // Check bias first - 00 = NT, 11 = T
        switch (hint.bias)
        {
          case 0b00:
            if (dprinf_pred)
            {
                DPRINTF(Whisper, "pc: %#0.10x -> NT (Bias=00)\n", pc);
            }
            return std::make_optional(false);
          case 0b11:
            if (dprinf_pred)
            {
                DPRINTF(Whisper, "pc: %#0.10x -> T (Bias=11)\n", pc);
            }
            return std::make_optional(true);
          default:
            break;
        }

        // If history length == 8, we can at least try
        if (hint.histLength() == 8)
        {
            std::bitset<1024> bitmask{mask(8)};
            std::bitset<8> hist{(globalHistory[tid] & bitmask).to_ulong()};

            auto pred = ROMBFUnit(hint.bool_formula_bits(), hist);
            if (dprinf_pred)
            {
                DPRINTF(Whisper,
                        "pc: %#0.10x -> Prediction: %s (Hist=%#0.4x)\n", pc,
                        pred ? "T" : "NT", hist.to_ulong());
            }
            return std::make_optional(pred);
        }

        // TODO
        if (dprinf_pred)
        {
            DPRINTF(Whisper, "pc: %#0.10x -> Not Implemented (HistLen=%u)\n",
                    pc, hint.histLength());
        }
    }

    return std::nullopt;
}

std::list<WhisperBP::HintBufferEntry>::const_iterator
WhisperBP::lookupBuffer(Addr pc) const
{
    return std::find_if(hintBuffer.cbegin(),
                      hintBuffer.cend(),
                      [pc](const auto &hint_entry)
                      {
                          return pc == hint_entry.addr;
                      });
}

void WhisperBP::markUsed(std::list<HintBufferEntry>::const_iterator it)
{
    if (it == hintBuffer.cend())
    {
        return;
    }

    hintBuffer.emplace_back(*it);
    hintBuffer.erase(it);
}

void WhisperBP::updateGlobalHistory(gem5::ThreadID tid, bool taken)
{
    if (globalHistory.find(tid) == globalHistory.cend())
    {
        globalHistory[tid] = std::bitset<1024>{};
    }

    globalHistory[tid] = (globalHistory[tid] << 1) | std::bitset<1024>(taken);
}

}  // namespace gem5::branch_prediction
