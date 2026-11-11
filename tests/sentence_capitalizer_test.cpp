#include <cassert>
#include <iostream>

#include "im/im.h"

#include "sentence_capitalizer.h"

int main() {
  areca::SentenceCapitalizationState state;

  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_period) ==
         areca::im::Key_period);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_space) ==
         areca::im::Key_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_space) ==
         areca::im::Key_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_a) ==
         areca::im::Key_A);
  assert(!state.capitalizeNextLetter);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_question);
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_b) ==
         areca::im::Key_B);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_period);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_c) ==
         areca::im::Key_c);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_period);
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_space);
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_Return);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_d) ==
         areca::im::Key_d);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_exclam);
  areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_BackSpace) ==
         areca::im::Key_BackSpace);
  assert(areca::capitalizeAfterSentenceBoundary(state, areca::im::Key_e) ==
         areca::im::Key_e);

  std::cout << "Sentence capitalizer tests passed\n";
  return 0;
}
