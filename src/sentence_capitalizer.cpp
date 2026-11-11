#include "sentence_capitalizer.h"

#include "im/im.h"

namespace areca {

im::KeySym
capitalizeAfterSentenceBoundary(SentenceCapitalizationState &state,
                                im::KeySym sym) {
  auto result = sym;
  if (state.capitalizeNextLetter) {
    if (sym >= im::Key_a && sym <= im::Key_z) {
      result = static_cast<im::KeySym>(sym - (im::Key_a - im::Key_A));
      state.capitalizeNextLetter = false;
    } else if (sym != im::Key_space) {
      state.capitalizeNextLetter = false;
    }
  }

  switch (result) {
  case im::Key_period:
  case im::Key_exclam:
  case im::Key_question:
    state.punctuationPending = true;
    break;
  case im::Key_space:
    if (state.punctuationPending) {
      state.capitalizeNextLetter = true;
      state.punctuationPending = false;
    }
    break;
  default:
    state.punctuationPending = false;
    break;
  }

  return result;
}

} // namespace areca
