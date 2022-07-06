// Copyright 2022, Joe Zbiciak <joe.zbiciak@leftturnonly.info>
// SPDX-License-Identifier:  CC-BY-SA-4.0
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using std::int64_t;

bool g_debug_branch_opt = false;

class VM {
 public:
  using LocType = int64_t;
  using ValueType = double;
  using ByteType = unsigned char;

  explicit VM(std::string_view prog) : prog_(prog) { Prescan(); }

  // Runs the program until completion.
  void Run() {
    do {
      terminate_ = false;
      Step();
    } while (!terminate_);
  }

  // Single-steps the program.
  bool Step();

  // Gets a variable, given its bytecode.
  ValueType GetV(ByteType var) const {
    return var_[var];
  }

  // Sets a variable to a given value.
  void SetV(ByteType var, ValueType val) {
    var_[var] = val;
  }

  // Gets the current PC.
  LocType GetPc() const {
    return pc_;
  }

  // Sets the current PC.
  void SetPc(LocType loc) {
    pc_ = loc;
  }

  // Gets a read-only reference to the stack_.
  const std::vector<ValueType>& GetStack() const {
    return stack_;
  }

  // Gets the total number of steps executed.  This is the actual step count,
  // and reflects any optimizations the prescanner performed, for example.
  int64_t GetSteps() const {
    return steps_;
  }

  // Gets the bytecode at a given PC.  Returns `X` (the termination bytecode)
  // if PC is out of range.
  ByteType ByteAt(LocType loc) const {
    if (loc < 0 || loc >= prog_.size()) {
      return kTerminateByte;
    }
    return prog_[loc];
  }

 private:
  using ValueLocPair = std::pair<ValueType, LocType>;

  static constexpr LocType kTerminatePc = std::numeric_limits<LocType>::max();
  static constexpr ByteType kTerminateByte = 'X';
  static constexpr ByteType kByteMax = std::numeric_limits<ByteType>::max();

  std::string prog_{};
  std::vector<LocType> branch_target_{};

  std::array<ValueType, kByteMax + 1> var_{};
  std::vector<ValueType> stack_{};
  std::map<LocType, ValueType> predec_values_{};
  std::map<double, LocType> global_label_{};
  LocType pc_ = 0;
  int64_t steps_ = 0;
  bool terminate_ = false;

  // Gets the next bytecode, advancing the PC.  Returns `X` (the termination
  // bytecode) if PC is out of range.
  ByteType NextByte() {
    if (pc_ < 0 || pc_ >= prog_.size()) {
      return kTerminateByte;
    }
    return prog_[pc_++];
  }

  // Pushes an item onto the stack_.
  void Push(double val) {
    stack_.push_back(val);
  }

  // Returns the top of stack_.  Underflowing the stack is not an error.  It
  // behaves as if there's an infinite well of 0s beneath it.
  ValueType Pop() {
    ValueType val = 0;
    if (!stack_.empty()) {
      val = stack_.back();
      stack_.pop_back();
    }
    return val;
  }

  // Returns a reference to top of stack_.  You must not use this reference
  // after a Push() or Pop().
  ValueType& Top() {
    if (stack_.empty()) {
      stack_.push_back(0.);
    }
    return stack_.back();
  }

  // Converts the double to an integer that fits within an int64_t.  Treats
  // NaN as 0.
  static int64_t Int(ValueType val) {
    double d = double(val);
    if (std::isnan(d)) {
      d = 0;
    }
    return int64_t(std::clamp(d, double(INT64_MIN), double(INT64_MAX)));
  }

  // Converts the double to an integer that fits within an uint64_t.  Treats
  // NaN as 0.
  static uint64_t Uint(ValueType val) {
    double d = double(val);
    if (std::isnan(d)) {
      d = 0;
    }
    return uint64_t(std::clamp(d, 0., double(UINT64_MAX)));
  }

  // Converts the double to a "Natural" number (non-negative integer) that
  // fits within an int64_t.  Treats NaN as 0.
  static int64_t Nat(ValueType val) {
    double d = double(val);
    if (std::isnan(d)) {
      d = 0;
    }
    return int64_t(std::clamp(d, 0., double(INT64_MAX)));
  }

  // Resolves a destination into a PC address.  Positive values correspond to
  // global labels, while negative values are the bitwise inverse of a PC
  // address.
  LocType Resolve(ValueType val) {
    double dst = double(val);
    if (dst < 0.) {
      return ~Int(dst);
    }

    if (std::isnormal(dst)) {
      auto it = global_label_.find(dst);
      if (it != global_label_.end()) {
        return it->second;
      }
    }

    return kTerminatePc;  // Not found?  Terminate.
  }

  // Drops the top N elements of the stack_.
  void DropN(int64_t n) {
    if (n > 0 && n < stack_.size()) {
      stack_.resize(stack_.size() - n);
    } else if (n >= stack_.size()) {
      stack_.clear();
    }
  }

  // Rotates the top N elements of the stack by extracting the Nth element
  // from the top, sliding everything else down, and pushing the extracted
  // element at the top.  0 leaves the stack unmodified, while 1 swaps the
  // top two elements.
  void Rotate(int64_t n) {
    if (n >= stack_.size()) {
      Push(0.);
    } else if (n > 0) {
      std::size_t idx = stack_.size() - n - 1;
      double val = stack_[idx];
      stack_.erase(stack_.begin() + idx);
      Push(val);
    }
  }

  // Prints the argument followed by a newline.
  static void PrintLn(ValueType val) {
    std::cout << val << '\n';
  }

  // Prints the argument.
  static void Print(ValueType val) {
    std::cout << val;
  }

  // Flatten whitespace down to ' '.
  static ByteType FixWs(ByteType bc) {
    return std::isspace(bc) ? ' ' : bc;
  }

  using DblFxn1 = double(double);
  using DblFxn2 = double(double, double);

  // Helper template for one-operand bytecodes.
  template <typename Callable>
  void OneOp(Callable fxn) {
    Top() = fxn(Top());
  }

  // Helper template for two-operand bytecodes.
  template <typename Callable>
  void TwoOp(Callable fxn) {
    auto rhs = Pop();
    Top() = fxn(Top(), rhs);
  }

  // Helper template for two-operand bytecodes.
  template <typename Callable>
  void TwoOpUint(Callable fxn) {
    auto rhs = Pop();
    Top() = fxn(Uint(Top()), Uint(rhs));
  }

  std::pair<ValueType, LocType> GetNumber(LocType loc);
  void Prescan();
};


// Parses a number in the bytecode stream at the given location in the bytecode
// stream.  Returns the number, and the location of the first bytecode after it.
VM::ValueLocPair VM::GetNumber(VM::LocType loc) {
  if (auto it = predec_values_.find(loc); it != predec_values_.end()) {
    return { it->second, branch_target_[loc + 1] };
  }

  enum NumState {
    kNsIdle, kNsInteger, kNsFraction, kNsExponent
  };

  const auto orig_loc = loc;

  auto num_state = kNsIdle;
  double val = 0.0;
  double p = 0.0;
  bool done = false;

  while (!done) {
    ByteType bytecode = ByteAt(loc++);

    switch (bytecode) {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        double digit_val = bytecode - '0';

        switch (num_state) {
          case kNsIdle: {
            val = digit_val;
            num_state = kNsInteger;
            continue;
          }
          case kNsInteger: {
            val = val * 10. + digit_val;
            continue;
          }
          case kNsFraction: {
            val += digit_val / p;
            p *= 10.;
            continue;
          }
          case kNsExponent: {
            p = p * 10. + digit_val;
            continue;
          }
        }
      }

      case '.': {
        switch (num_state) {
          case kNsIdle: case kNsInteger: {
            num_state = kNsFraction;
            p = 10.;
            continue;
          }
          case kNsFraction: {
            num_state = kNsExponent;
            p = 0.;
            continue;
          }
          case kNsExponent: {
            val *= std::pow(10., int(p));
            done = true;
            continue;
          }
        }
      }

      default: {
        loc--;  // Back up, as we just passed a non-numeric bytecode.
        done = true;
      }
    }
  }

  predec_values_[orig_loc] = val;
  branch_target_[orig_loc + 1] = loc;
  return { val, loc };
}


// Prescans the program, establishing the location of all global and local
// labels, and the values of all numbers.  This allows for fast lookup
// without scanning at run-time.
//
// Local reverse branches are resolved in the forward pass.  Local forward
// branches are resolved in the reverse pass.  Each pass keeps track of the
// most recent instance of each local label it sees during that pass, making
// for O(1) lookup.
//
// Conditional branches are resolved during the reverse pass.  Crossing a ';'
// increments our nesting depth, and sets the ';' and ':' targets to the
// location after the ';' for that depth.  Crossing a ':' sets the ':' for the
// current depth, and sets the branch target for ':' to the ';' target.
// Crossing a '?' sets the branch target to '?' for the first byte after the
// most recent ':' at this depth.
//
// Branches to unconditional branches can be resolved down to a single branch.
//
// Note:  Global branches can't be resolved since they draw their argument from
// the stack.  Predecoding literals gets us most of that anyway.
//
// This should only be called once, from the contructor.
void VM::Prescan() {
  struct ThenElse {
    LocType after_then;
    LocType after_else;
  };
  std::vector<ThenElse> then_else;
  then_else.push_back({kTerminatePc, kTerminatePc});

  // Branch target array is indexed by PC after fetching the bytecode (PC+1).
  branch_target_.resize(prog_.length() + 1, kTerminatePc);

  // Forward pass.
  std::array<LocType, kByteMax + 1> recent_local{};
  std::fill(recent_local.begin(), recent_local.end(), kTerminatePc);
  for (LocType loc = 0; loc != prog_.size();) {
    const ByteType bytecode = FixWs(ByteAt(loc++));

    switch (bytecode) {
      case 'L': { recent_local[ByteAt(loc)] = loc + 1; break; }
      case 'B': { branch_target_[loc] = recent_local[ByteAt(loc)]; break; }

      case '@': {
        auto [val, new_loc] = GetNumber(loc);
        global_label_[val] = new_loc;
        branch_target_[loc] = new_loc;
        // In the unlikely event someone jumps into the middle of a global label
        // definition, GetNumber will do the right thing.  For now, optimize
        // for the more likely case.
        loc = new_loc;
        break;
      }

      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': case '.': {
        auto [val, new_loc] = GetNumber(loc - 1);
        loc = new_loc;
        break;
      }
    }
  }

  // Reverse pass.
  std::fill(recent_local.begin(), recent_local.end(), kTerminatePc);
  ByteType prevbyte = kTerminateByte;
  LocType last_non_whitespace = kTerminatePc;
  LocType lnw1 = kTerminatePc, lnw2 = kTerminatePc;
  for (LocType loc = prog_.size(); loc > 0;) {
    // Force all whitespace to be exactly ' ' for switch-case. 
    const LocType lloc = loc;
    const ByteType currbyte = ByteAt(--loc);
    const ByteType bytecode = FixWs(currbyte);

    if (bytecode != ' ' && bytecode != ';') {
      lnw2 = lnw1;
      lnw1 = last_non_whitespace;
      last_non_whitespace = loc;
    }

    switch (bytecode) {
      case 'L': {
        branch_target_[lloc] = lnw2;
        recent_local[prevbyte] = loc + 2;
        break;
      }
      case 'F': { branch_target_[lloc] = recent_local[prevbyte]; break; }

      case ';': {
        branch_target_[lloc] = last_non_whitespace;
        then_else.push_back({last_non_whitespace, last_non_whitespace});
        break;
      }

      case ':': {
        branch_target_[lloc] = then_else.back().after_else;
        then_else.back().after_then = lnw1;
        break;
      }

      case '?': {
        branch_target_[lloc] = then_else.back().after_then;
        if (then_else.size() > 1) {
          then_else.pop_back();
        }
        break;
      }

      case ' ': {
        branch_target_[lloc] = last_non_whitespace;
        break;
      }
    }

    prevbyte = currbyte;  // Without whitespace remap in case of dodgy labels.
  }

  // Branch-to-branch pass.
  std::vector<LocType> branch_froms;
  for (LocType loc = 0; loc != prog_.size();) {
    const ByteType bytecode = ByteAt(loc++);
    LocType branch_from_loc = loc;
    LocType branch_target_loc = branch_target_[loc];

    branch_froms.clear();

    while (branch_target_loc != kTerminatePc) {
      ByteType target_byte = FixWs(ByteAt(branch_target_loc));

      if (target_byte == 'L' || target_byte == 'F' || target_byte == 'B' ||
          target_byte == '@' || target_byte == ':' || target_byte == ' ' ||
          target_byte == 'X' || target_byte == ';') {
        if (g_debug_branch_opt) {
          std::cout << "loc=" << loc << " tb='" << target_byte << "' bfl="
                    << branch_from_loc << " btl=" << branch_target_loc;
        }
        branch_froms.push_back(branch_from_loc);
        branch_from_loc = branch_target_loc + 1;
        // Force it for `X` as it might be outside the program image.
        branch_target_loc =
            target_byte == 'X' ? kTerminatePc : branch_target_[branch_from_loc];
        if (g_debug_branch_opt) {
          std::cout << " => bfl=" << branch_from_loc
                    << " btl=" << branch_target_loc << '\n';
        }
      } else {
        break;
      }
    }

    if (branch_target_[loc] != branch_target_loc) {
      for (auto from : branch_froms) {
        if (g_debug_branch_opt) {
          std::cout << "remap pc=" << from << " old=" << branch_target_[from]
                    << " new=" <<  branch_target_loc << '\n';
        }
        branch_target_[from] = branch_target_loc;
      }
    }
  }

  // Branch-to-global-label pass.  If a global label points to an unconditional
  // branch, we can redirect the global label to its ultimate target as well.
  // At this point we don't need to step through the chain of targets as that's
  // just been flattened.
  for (auto& [label, target] : global_label_) {
    ByteType target_byte = FixWs(ByteAt(target));
    if (target_byte == 'L' || target_byte == 'F' || target_byte == 'B' ||
        target_byte == '@' || target_byte == ':' || target_byte == ' ' ||
        target_byte == 'X' || target_byte == ';') {
      if (g_debug_branch_opt) {
        std::cout << "remap lbl=" << label << " old=" << target << " new="
                  << branch_target_[target + 1] << '\n';
      }
      target = branch_target_[target + 1];
    }
  }
}

bool VM::Step() {
  int bytecode = FixWs(NextByte());
  // Floating point escape bytecodes.
  auto Esc = [](ByteType b) { return b + kByteMax + 1; };

  steps_++;

  if (bytecode == '\\') {
    bytecode = Esc(NextByte());
  }

  switch (bytecode) {
    case 'X': {
      terminate_ = true;
      break;
    }

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': case '.': {
      auto [val, new_loc] = GetNumber(pc_ - 1);
      pc_ = new_loc;
      Push(val);
      break;
    }

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z': {
      Push(GetV(bytecode));
      break;
    }

    case '+': { TwoOp(std::plus()); break; }
    case '-': { TwoOp(std::minus()); break; }
    case '*': { TwoOp(std::multiplies()); break; }
    case '/': { TwoOp(std::divides()); break; }
    case '~': { OneOp(std::negate()); break; }
    case '%': { TwoOp<DblFxn2>(std::fmod); break; }
    case '&': { TwoOpUint(std::bit_and()); break; }
    case '|': { TwoOpUint(std::bit_or()); break; }
    case '^': { TwoOpUint(std::bit_xor()); break; }
    case '<': { auto rhs = Pop(); Top() *= std::exp2(rhs); break; }
    case '>': { auto rhs = Pop(); Top() /= std::exp2(rhs); break; }
    case '\'': { PrintLn(Top()); break; }
    case '!': { PrintLn(GetV(NextByte())); break; }
    case 'C': { auto dst = Resolve(Pop()); Push(~pc_); pc_ = dst; break; }
    case 'G': { pc_ = Resolve(Pop()); break; }
    case 'I': { Top() = Int(Top()); break; }
    case 'U': { Top() = Uint(Top()); break; }
    case 'M': { SetV(NextByte(), Pop()); break; }
    case 'V': { Push(GetV(NextByte())); break; }
    case 'D': { Push(Top()); break; }
    case 'P': { Pop(); break; }
    case 'Q': { DropN(Nat(Pop())); break; }
    case 'R': { Rotate(Nat(Pop())); break; }
    case 'S': { auto a = Pop(), b = Pop(); Push(a); Push(b); break; }
    case '?': { if (Pop() < 0) { pc_ = branch_target_[pc_]; } break; }
    case 'L': case '@': case ':': case 'B': case 'F': case ' ': case ';': {
      pc_ = branch_target_[pc_]; break;
    }

    // Library escapes.
    case Esc('^'): { TwoOp<DblFxn2>(std::pow); break; }
    case Esc('h'): { TwoOp<DblFxn2>(std::hypot); break; }
    case Esc('H'): {
      auto x = Pop(), y = Pop();
      Top() = std::hypot(Top(), y, x);
      break;
    }
    case Esc('a'): { TwoOp<DblFxn2>(std::atan2); break; }
    case Esc('s'): { OneOp<DblFxn1>(std::sin); break; }
    case Esc('S'): { OneOp<DblFxn1>(std::asin); break; }
    case Esc('c'): { OneOp<DblFxn1>(std::cos); break; }
    case Esc('C'): { OneOp<DblFxn1>(std::acos); break; }
    case Esc('t'): { OneOp<DblFxn1>(std::tan); break; }
    case Esc('T'): { OneOp<DblFxn1>(std::atan); break; }
    case Esc('x'): { OneOp<DblFxn1>(std::sinh); break; }
    case Esc('X'): { OneOp<DblFxn1>(std::asinh); break; }
    case Esc('y'): { OneOp<DblFxn1>(std::cosh); break; }
    case Esc('Y'): { OneOp<DblFxn1>(std::acosh); break; }
    case Esc('z'): { OneOp<DblFxn1>(std::tanh); break; }
    case Esc('Z'): { OneOp<DblFxn1>(std::atanh); break; }
    case Esc('v'): { OneOp<DblFxn1>(std::erf); break; }
    case Esc('V'): { OneOp<DblFxn1>(std::erfc); break; }
    case Esc('u'): { OneOp<DblFxn1>(std::tgamma); break; }
    case Esc('U'): { OneOp<DblFxn1>(std::lgamma); break; }
    case Esc('e'): { OneOp<DblFxn1>(std::exp); break; }
    case Esc('l'): { OneOp<DblFxn1>(std::log); break; }
    case Esc('2'): { OneOp<DblFxn1>(std::log2); break; }
    case Esc('q'): { OneOp<DblFxn1>(std::sqrt); break; }
    case Esc('3'): { OneOp<DblFxn1>(std::cbrt); break; }
    case Esc('>'): { OneOp<DblFxn1>(std::ceil); break; }
    case Esc('<'): { OneOp<DblFxn1>(std::floor); break; }
    case Esc('_'): { OneOp<DblFxn1>(std::trunc); break; }
    case Esc('|'): { OneOp<DblFxn1>(std::abs); break; }
    case Esc('i'): { OneOp<DblFxn1>(std::round); break; }
    case Esc('I'): { OneOp<DblFxn1>(std::nearbyint); break; }
    case Esc('f'): {
      int exp;
      Top() = std::frexp(Top(), &exp);
      Push(exp);
      break;
    }
    case Esc('F'): {
      auto rhs = Pop();
      Top() = std::ldexp(Top(), rhs);
      break;
    }
    case Esc('m'): {
      double int_part;
      Top() = std::modf(Top(), &int_part);
      Push(int_part);
      break;
    }
    case Esc('-'): { OneOp<bool(double)>(std::signbit); break; }
    case Esc('+'): {
      auto rhs = Pop();
      Top() = std::copysign(Top(), rhs);
      break;
    }

    default: {
      std::cout << "Undefined bytecode '" << bytecode << "' at " << pc_ - 1
                << ". Terminating.\n";
      terminate_ = true;
    }
  }

  return terminate_;
}

static void ShowTop5(const std::vector<VM::ValueType>& stack) {
  std::size_t first = 0;
  std::size_t last = stack.size();
  if (last - first > 5) {
    first = last - 5;
  }

  for (std::size_t i = first; i < last; ++i) {
    std::cout << ' ' << stack[i];
  }
}

}  // namespace

int main(int argc, char *argv[]) {
  std::string prog, line;

  // Read the program on stdin.
  while (std::getline(std::cin, line)) {
    prog += line;
    prog += ' ';  // Preserve whitespace between lines.
  }

  g_debug_branch_opt = argc > 1 && argv[1][0] == 'b';

  auto vm = VM(prog);

  if (argc == 1) {
    vm.Run();
  } else {
    bool terminate;
    do {
      VM::LocType pc = vm.GetPc();
      std::cout << "PC=" << pc << " '" << vm.ByteAt(pc) << "' ";
      ShowTop5(vm.GetStack());
      std::cout << '\n';
      terminate = vm.Step();
    } while (!terminate);
  }

  std::cout << "DONE.  " << vm.GetSteps() << " steps\n";
}
