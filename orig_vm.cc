#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
 
enum NumState {
  kNsIdle, kNsInteger,
  kNsFraction, kNsExponent 
};

int main() {
  std::string prog, line;

  // Read the program on stdin.
  while (std::getline(std::cin, line)) {
    prog += line;
  }

  // Add one dummy NOP at the end,
  // so we can always safely fetch
  // our 'v' arg when needed.
  prog += ' ';

  // Machine state:
  std::vector<double> stack;
  std::array<double, 26> vars{};
  std::size_t pc = 0;
  double P = 0;
  enum NumState num_state = kNsIdle;
  bool make_num_idle = true;
  
  // Helpers:
  auto top = [&stack]() -> double& {
    if (stack.empty()) {
      stack.push_back(0);
    }
    return stack.back();
  };

  auto pop = [&stack]() {
    double val = 0;
    if (!stack.empty()) {
      val = stack.back();
      stack.pop_back();
    }
    return val;
  };

  auto push = [&stack](double val) {
    stack.push_back(val);
  };

  auto get_v = [&prog,&pc]() {
    int v = prog[pc++] - 'a';
    if (v < 0 || v > 25) {
      v = 0;
    }
    return v;
  };

  // Interpreter loop.
  while (pc < prog.length()) {
    char opcode = prog[pc++];

    // Handle num_state idle transition.
    if (make_num_idle) {
      num_state = kNsIdle;
    }
    make_num_idle = true;

    // Whitespace is a no-op.
    if (std::isspace(opcode)) {
      continue;
    }

    // Letters 'a' through 'z'
    // just push the value on the
    // stack.  Assumes ASCII.
    if (opcode >= 'a' &&
        opcode <= 'z') {
      push(vars[opcode - 'a']);
      continue;
    }

    // Execute other opcodes.
    switch (opcode) {
      case '0': case '1': case '2':
      case '3': case '4': case '5':
      case '6': case '7': case '8':
      case '9': {
        int digit_val = opcode - '0';
        make_num_idle = false;

        switch (num_state) {
          case kNsIdle:
            push(digit_val);
            num_state = kNsInteger;
            continue;

          case kNsInteger:
            top() = top() * 10 + digit_val;
            continue;

          case kNsFraction:
            top() += digit_val / P;
            P *= 10;
            continue;

          case kNsExponent:
            P = P * 10 + digit_val;
            continue;
        }
      }

      case '.': {
        make_num_idle = false;

        switch (num_state) {
          case kNsIdle:
            push(0);
            // FALLTHROUGH INTENDED
          case kNsInteger:
            num_state = kNsFraction;
            P = 10;
            continue;

          case kNsFraction:
            num_state = kNsExponent;
            P = 0;
            continue;

          case kNsExponent:
            num_state = kNsIdle;
            top() *= std::pow(10, int(P));
            continue;
        }
      }

      case '\'': {
        std::cout << top() << '\n';
        continue;
      }

      case '!': {
        std::cout << vars[get_v()] << '\n';
        continue;
      }

      case '~': {
        top() *= -1;
        continue;
      }

      case '+': {
        auto a = pop();
        top() += a;
        continue;
      }

      case '-': {
        auto a = pop();
        top() -= a;
        continue;
      }

      case '*': {
        auto a = pop();
        top() *= a;
        continue;
      }         
      
      case '/': {
        auto a = pop();
        top() /= a;
        continue;
      }
 
      case 'D': {
        push(top());
        continue;
      }

      case 'I': {
        top() = std::trunc(top());
        continue;
      }

      case 'M': {
        vars[get_v()] = pop();
        continue;
      }

      case 'P': {
        pop();
        continue;
      }

      case 'Q': {
        long count = pop();
        if (count > 0 && count < stack.size()) {
          stack.resize(stack.size() - count);
        } else if (count >= stack.size()) {
          stack.clear();
        }
        continue;
      }

      case 'R': {
        long n = pop();
        if (n >= stack.size()) {
          push(0);
        } else if (n > 0) {
          std::size_t idx = stack.size() - n - 1;
          double val = stack[idx];
          stack.erase(stack.begin() + idx);
          push(val);
        }
        continue;
      }

      case 'S': {
        auto a = pop();
        auto b = pop();
        push(a);
        push(b);
        continue;
      }

      case '?': {
        if (pop() < 0) {
          // Scan forward until we pass ':'.
          int deep = 0;
          while (pc < prog.length()) {
            const int op = prog[pc++];
            if (op == '?') {
              ++deep;
            } else if (op == ';') {
              --deep;
            } else if (op == ':' && !deep) {
              break;
            }
          }
        }
        continue;
      }

      case ':': {
        // Scan forward until we pass ';'.
        int deep = 0;
        while (pc < prog.length()) {
          const int op = prog[pc++];
          if (op == '?') {
            ++deep;
          } else if (op == ';') {
            if (!--deep) {
              break;
            }
          }
        }
        continue;
      }

      case 'X': {
        // Place PC outside program image.
        pc = std::string::npos;
        continue;
      }

      case 'L': {
        // act as a NOP but advance past v.
        get_v();
        continue;
      }

      case 'B': {
        int v = 'a' + get_v();
        // Scan backward until we find L [v].
        while (--pc > 0) {
          if (prog[pc] == v &&
              prog[pc - 1] == 'L') {
            pc++;
            break;
          }
        }
        continue;
      }

      case 'F': {
        int v = 'a' + get_v(); 
        // Scan forward until we find L [v].
        while (++pc < prog.size()) {
          if (prog[pc] == v &&
              prog[pc - 1] == 'L') {
            pc++;
            break;
          }
        }
        continue;
      }
    }
  }

  std::cout << "DONE\n";
}
