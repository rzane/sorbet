# typed: true
# selective-apply-code-action: refactor.extract

class A; 1 + 123; end
#            ^^^ apply-code-action: [A] Extract Variable

class B; 1 + 1; 1 + 123; end
#                   ^^^ apply-code-action: [B] Extract Variable

class C
  1 + 123
#     ^^^ apply-code-action: [C] Extract Variable
end

class D
  1 + 1
  1 + 123
#     ^^^ apply-code-action: [D] Extract Variable
end

class E
  1 + 1
  1+ 2; 1 + 123
#           ^^^ apply-code-action: [E] Extract Variable
end