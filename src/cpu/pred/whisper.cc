#include "cpu/pred/whisper.hh"

#include "debug/Whisper.hh"

namespace gem5::branch_prediction
{

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
    updateGlobalHistory(tid, taken);

    auto hint_it = lookupBuffer(pc);
    if (hint_it == hintBuffer.end())
    {
        fallbackPredictor->updateHistories(tid, pc, uncond, taken, target,
                                           bp_history);
    }
}

bool WhisperBP::lookup(ThreadID tid, Addr pc, void *&bp_history) {
    auto hint_it = lookupBuffer(pc);
    markUsed(hint_it);
    if (hint_it != hintBuffer.end())
    {
        Hint hint = Hint::fromUInt(hint_it->hint);

        // Check bias first - 00 = NT, 11 = T
        switch (hint.bias)
        {
          case 0b00:
            DPRINTF(Whisper, "pc: %#0.10x -> Bias: 00\n", pc);
            return false;
          case 0b11:
            DPRINTF(Whisper, "pc: %#0.10x -> Bias: 11\n", pc);
            return true;
          default:
            break;
        }

        // TODO
        DPRINTF(Whisper, "pc: %#0.10x -> Not Implemented\n", pc);
    }

    return fallbackPredictor->lookup(tid, pc, bp_history);
}

void WhisperBP::update(ThreadID tid, Addr pc, bool taken,
                       void *&bp_history, bool squashed,
                       const StaticInstPtr &inst, Addr target)
{
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

std::list<WhisperBP::HintBufferEntry>::iterator
WhisperBP::lookupBuffer(Addr pc)
{
    return std::find_if(hintBuffer.begin(),
                      hintBuffer.end(),
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
