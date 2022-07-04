# Intro

This project contains a simple stack-based, bytecode-oriented virtual machine
that I initially developed for 
[this Quora answer.](https://www.quora.com/Interpreted-languages-typically-rely-on-virtual-machines-which-are-hardware-independent-How-does-that-work-seeing-as-all-code-has-to-eventually-be-executed-on-a-physical-device/answer/Joe-Zbiciak).

The file `orig_vm.cc` contains the original VM I developed on the fly in about
2 - 3 hours of furious typing in Quora's editor, plus subsequent bug fixes.
Let's just say it's not well suited for entering code, and it would have gone
faster if I'd used a proper editor.

The file `vm.cc` is the same virtual machine, reformatted for displays wider
than a cash register receipt and cleaned up overall, with a couple additional
bytecodes and features added.  These are noted in the description below.

## Machine Model

### Data Stack and Registers

The VM implements a stack machine with an arbitrarily deep data stack.  The
stack behaves as if there is an infinite series of zeros beneath it, so it's
impossible to underflow the stack.

The VM also offers 26 registers, named `a` through `z`.  Each
register holds a single value.  The bytecodes `a` through `z` push the
corresponding register onto the stack.

The VM doesn't let you act directly on registers.  You can pop values from
the stack into registers, or push copies of register contents onto the stack.
That's it.  Registers can be a convenient place to store regularly used
constants.

### Values

In the original VM, all values are double precision IEEE 754 floating point
values.  In the updated VM, I plan to allow strings by using NaN-boxed
references to a string table.

### Machine State

The VM carries some additional state to track its execution:

* `PC` is the program counter.  It holds the offset from the beginning of the
  program text.
* `P` and `NumState` are state variables used by the number parsing state
  machine. `P` stands for _Power,_ and is used for holding either the current
  power-of-10 to apply to a fraction digit, or the current exponent value for an
  exponent under construction.

### Argument Order

For bytecodes that extract multiple arguments from the stack, the right-most
argument is at the top of stack, and the left-most is furthest down in the
stack.  This is consistent to pushing arguments left to right.

For binary arithmetic operators, that means the right-hand argument is on top
of stack, and the left-hand argument is just beneath it.

## Bytecode Reference

### Conventions 

The term TOS refers to the value on the top of stack, and NOS refers to the
next value on the stack below it ("next on stack").

The notation _v_ after a bytecode refers to a single-character variable `a`
through `z` that appears in the bytestream after the bytecode.  An invalid _v_
byte acts as if you supplied `a`.  

The notation _n_ before a bytecode refers to a value popped from TOS and
truncated to an integer.  Negative values map to 0.  The value _n_ does not
come from the bytecode stream.  That said, if you write a numeric literal
in the bytecode stream ahead of this bytecode, the VM will push that literal
onto the stack so it can serve as an argument to the bytecode.  These arguments
are used with bytecodes that need counts or indices.

The notation _l_ after a bytecode refers to a single-character label.  In the
original VM, this label is nearly equivalent to _v._  In the case of labels
formed by the `L` bytecode in the original VM, an invalid value for _v_ does
nothing, and does not form a label.  In the updated VM, _l_ allows all
character values to act as labels.  Single-character labels are intended for
local branches.

*New:* The notation _x_ after a bytecode refers to an global label.  Extended
labels have the same syntax as numeric literals.  As with numeric literals,
global labels cannot be negative.  The _x_ argument appears after the
bytecode, as it must not be computed dynamically.

*New:* The notation _d_ refers to a destination popped from TOS.  Infinities
and NaNs terminate the program.  Negative values are truncated to integers
and bitwise inverted to determine the new PC.  Non-negative values values
represent the corresponding global label _x_.  The VM resolves this global
label to a valid PC value.  Dereferencing a missing global label terminates
the program.  The last instance of a duplicated global label is the one
resolved.  Global labels are intended for long-distance jumps and calls.
Absolute PC values are intended for function pointers and returns.

### Pseudo-code Functions

The bytecode definitions use the following pseudo-code functions:

| Function | Description |
| :---: | :--- |
| `Top()` | Returns the value at the top of stack without popping it |
| `Pop()` | Pops the item on the top of stack and returns it. |
| `Push(x)` | Pushes `x` onto the stack. |
| `Int(x)` | Truncates `x` to a 2's complement 64-bit integer.  Clamps values to the the representable 64-bit integer range.  NaN maps to 0. |
| `Nat(x)` | Like `Int(x)`, only it clamps negative values to 0. |
| `Pow(x,y)` | Raises `x` to the `y`th power. |
| `FMod(x,y)` | Returns the floating point remainder after `x/y`, in the same manner as [C and C++.](https://en.cppreference.com/w/cpp/numeric/math/fmod) |
| `Resolve(x)` | Resolves a destination into a PC address, as per the rules of _d_ above. |
| `PrintLn(x)` | Prints `x` followed by a newline. |
| `Print(x)` | Prints `x` without a newline. |
| `GetV(v)` | Gets the value of variable _v_. |
| `SetV(v,x)` | Sets the value of variable _v_ to `x`. |
| `Repeat(n):` | Repeats the following statement `n` times. |
| `Rotate(x)` | Extracts the element `x` units below TOS and pushes it on top. |

### Summary

| Bytecode | Description | New? |
| :--: | :-- | :--: |
| `0`, `1`, `2`, `3`, `4`, `5`, `6`, `7`, `8`, `9`, `.` | Numeric digits and `.` form numeric constants. | n |
| `+` | `TOS = Pop(); NOS = Pop(); Push(NOS + TOS);` | n |
| `-` | `TOS = Pop(); NOS = Pop(); Push(NOS - TOS);` | n |
| `*` | `TOS = Pop(); NOS = Pop(); Push(NOS * TOS);` | n |
| `/` | `TOS = Pop(); NOS = Pop(); Push(NOS / TOS);` | n |
| `~` | `TOS = Pop(); Push(-TOS);`  | n |
| `%` | `TOS = Pop(); NOS = Pop(); Push(FMod(NOS, TOS));` | YES |
| `&` | `TOS = Pop(); NOS = Pop(); Push(Int(NOS) & Int(TOS));` | YES |
| `\|` | `TOS = Pop(); NOS = Pop(); Push(Int(NOS) | Int(TOS));` | YES |
| `^` | `TOS = Pop(); NOS = Pop(); Push(Int(NOS) ^ Int(TOS));` | YES |
| `<` | `TOS = Pop(); NOS = Pop(); Push(NOS * Pow(2, TOS));` | YES |
| `>` | `TOS = Pop(); NOS = Pop(); Push(NOS / Pow(2, TOS));` | YES |
| `'` | `PrintLn(Top())`. Displays value of TOS on line by itself. | n |
| `!` _v_ | `PrintLn(GetV(v));` Displays the value of variable _v_ on a line by itself. | n |
| `@` _x_ | Defines the global label _x._  | YES |
| _d_ `C` | *Call.* `TOS = Pop(); Push(~(PC + 1)); PC = Resolve(d);` Makes a function call by jumping to a destination while pushing a return address. | YES |
| `D` | `Push(Top());` Duplicates TOS. | n |
| `I` | `TOS = Pop(); Push(Int(TOS));`  Truncates TOS to an integer. | n |
| _d_ `G` | *Goto.* `TOS = Pop(); PC = Resolve(TOS);`  Sets `PC` to the resolved address of _d_. | YES |
| `M` _v_ | `TOS = Pop(); SetV(v, TOS);`  Pops TOS into variable _v._ | n |
| `P` | `Pop();` Pops TOS from the stack. | n |
| _n_ `Q` | `TOS = Pop(); Repeat(Nat(TOS)): Pop();` Pops the next _n_ values from the stack. | n |
| _n_ `R` | `TOS = Pop(); Rotate(Nat(TOS));` Rotates the top _n_ elements of the stack. | n |
| `S` | `Rotate(1);` Swaps the top two elements of the stack. | n |
| `?` | Consumes TOS. If it's negative, it skips ahead to the next `:` (_new:_ or `;`) at the same nesting level and resumes execution after it. | Modified |
| `:` | Skips ahead to the next `;` at the same nesting level and resumes execution after it. | n |
| `;` | NOP.  Serves as marker for `:`. | n |
| `L` _l_ | NOP.  Serves as marker for label _l._  | Modified |
| `B` _l_ | Jumps backward to previous `L` _l_.  Restarts (old) or terminates (new) program if label not found. | Modified |
| `F` _l_ | Jumps forward to next `L` _l_.  Terminates program if label not found. | Modified |
| `X` | Terminates execution. | n |
| _whitespace_ | NOP. Also terminates the numeric entry state machine. | n |

## Control flow

### Unconditional local branches

The `L` bytecode defines a local label.  In the original interpreter, this was
constrained to the same set of names associated with variables: `a` through `z`.
The updated interpreter relaxes that restriction to allow all byte values.

The `F` and `B` bytecodes provide unconditional branches.  They scan forward or
backward for a particular label defined by the `L` bytecode.

These are meant primarily for local branches.  You can reuse the same label as
many times as you wish.  The `F` and `B` keywords find the *nearest* instance
of a particular label.

Nothing stops you from using these for farther-reaching branches; however, the
limited set of label names makes that tricky.

### If-Then-Else

The `?`, `:`, and `;` bytecodes form the an *if-then-else* sequence.  The `?` 
determines whether to take the *then* or *else* branch based on the sign of
the value at top of the stack.  Non-negative values take the *then* branch,
while negative values take the *else* branch.

In the original VM, all three bytecodes are expected to appear together, even
if the *else* branch is empty.  In the updated VM, the `:` is optional.  That
is, the construct `?` _then_ `;`  provides an *if-then* statement.

Nothing enforces proper pairing of these bytecodes.  The `?` operator skips
forward to the first `:` (or `;` in the updated VM) at the same nesting level
if its argument is negative.  The `:` bytecode scans forward to the first `;`
at the same nesting level.

When scanning forward, the `?` and `:` bytecodes track nesting level by
counting `?` and `;` bytecodes.  Each `?` increases the nesting level, and
each `;` decreases the nesting level.  In the original VM, the construct
`?` _then_ `;` would actually make the nesting level negative if the argument
to `?` was negative.  That could lead to wacky behavior.  In the updated VM,
it's impossible for the nesting level to become negative.

A `:` opcode always skips forward to its matching `;`.  That means code such
as the following has well defined behavior:

```
1~ ? La 42'P : 17'P Ba ; 
```

This will print 17 followed by 42, and then terminate.  Branching into the
middle of or out of an *if-then-else* is perfectly fine.

### Loops

The bytecode does not offer an explicit looping construct.  Rather, use an
*if-then* or *if-then-else* coupled with local branch.

The following example loops 10 times, printing 42 each iteration.  The loop
counter is kept on the top of stack across loop iterations.

```
9 La 42'P 1- D? Ba ;
```

Note: in the original VM, you need to write:

```
9 La 42'P 1- D? Ba :;
```

### Unconditional long branches and calls _(New)_

#### Destinations

Destinations fall into two categories:  Global labels and absolute program
addresses.

Global labels look just like any other numeric literal.  As with other numeric
literals, global labels are always non-negative.  The full range of
non-negative double-precision normal and subnormal values is available for
labels; however, this specification recommends sticking to the exact integer
range [1, 2⁵³].

Absolute program addresses are typically created at run time.  When consuming a 
destination, the VM distinguishes a label reference from a PC address by its
sign.  Positive values are global labels, while negative values are encoded
program addresses.  For the latter, the encoded value corresponds to -(PC + 1).

This specification recommends avoiding 0, so that there's no possibility of
strange effects around signed zero.  That also leaves the value 0 available to
signal a null function pointer, for example.

#### 

The `@` bytecode defines an global label.  The global label follows the opcode.
Never comes from the stack.  The label is fixed in the bytecode.

The `G` bytecode performs an unconditional jump (aka. _goto_)  to the
destination specified at TOS.  That destination is decoded as described under
*Destinations* above.

The `C` bytecode performs an unconditional *call* to the destination specified
at TOS.  That destination is decoded as described under *Destination* above.
The `C` bytecode pushes the address of the next bytecode on the stack, encoded
as a destination.  That allows a subsequent `G` bytecode to return to the
caller.

None of these opcodes interacts with local labels.

**Example:**

```
17 La 100C 1- D ? Ba : X ;

@100 42'P G
```

This prints 42 a total of 18 times by calling the subroutine `@100`.

The `S` and `R` opcodes can be used to access arguments passed by the caller
via the stack.  Suppose you have a function that computes _ax² + bx + c,_ for a
given _a,_ _b,_ _c_ and _x._  Assume the arguments are pushed in that order:

```
1 2 3 4 100C ' X

@100 
S
DD* 
5R*S
4R*+
2R+S
G
```

At the point we reach `@100` the stack contains 1, 2, 3, 4, _d_, where _d_ is
the destination associated with the return address.  The `S` opcode brings the
value for _x_ to the top of stack, yielding 1, 2, 3, _d_, 4.  The sequence
`DD*` replicates _x_ twice and then multiplies the two copies, computing _x²._
The stack now contains 1, 2, 3, _d_, 4, 16.

The sequence `5R*S` brings _a_ to the TOS, multiplies it with _x²,_ and then
swaps it under _x._  The stack now contains 2, 3, _d_, 16, 4.

The sequence `4R*+` brings _b_ to the TOS, multiplies it with _x_, and then
adds it to _ax²._  The stack now contains 3, _d_, 24.

Finally, the sequence `2R+S` brings _c_ to the TOS, and adds it to _ax²+b._ It
then swaps the sum under the return address.  The stack now contains 27, _d_.

The final `G` then returns.


