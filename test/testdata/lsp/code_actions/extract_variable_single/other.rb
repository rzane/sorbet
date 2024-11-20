# typed: true
# selective-apply-code-action: refactor.extract
# enable-experimental-lsp-extract-to-variable: true

if 1 == 2
  1 + 5
#     ^ apply-code-action: [A] Extract Variable (this occurrence only)
else
  6 + 3
# ^ apply-code-action: [B] Extract Variable (this occurrence only)
end

y = 2
a = (x = 1; x + y)
#           ^ apply-code-action: [C] Extract Variable (this occurrence only)

while 1 == 2
  x = a + y
#     ^^^^^ apply-code-action: [D] Extract Variable (this occurrence only)
end

for i in 1..7
  puts i + 1
#      ^^^^^ apply-code-action: [E] Extract Variable (this occurrence only)
end

unless 1 == 2
  puts 2 + 3
#      ^^^^^ apply-code-action: [F] Extract Variable (this occurrence only)
end
