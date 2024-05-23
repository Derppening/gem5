#include "cpu/pred/whisper.hh"

namespace gem5::branch_prediction
{

WhisperBP::WhisperBP(const WhisperBPParams &params)
    : BPredUnit(params),
      hintBufferSize{params.hint_buffer_size},
      hintBuffer{},
      fallbackPredictor{params.fallback_predictor}
{}

void WhisperBP::updateHistories(ThreadID tid, Addr pc, bool uncond,
                                bool taken, Addr target,
                                void *&bp_history)
{
    // Whisper does not handle unconditional branches
    if (uncond)
    {
        fallbackPredictor->updateHistories(tid, pc, uncond, taken, target,
                                           bp_history);
        return;
    }

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
        // TODO
        return false;
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
}

void WhisperBP::insert(Addr pc, uint32_t hint)
{
    // Ensure that the hint buffer size does not exceed max size
    while (hintBuffer.size() >= hintBufferSize)
    {
        hintBuffer.pop_front();
    }

    hintBuffer.emplace_back(HintBufferEntry{pc + (hint & mask(12)), hint});
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

}  // namespace gem5::branch_prediction
