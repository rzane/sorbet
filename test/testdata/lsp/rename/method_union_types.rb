# typed: strict
# frozen_string_literal: true
require 'sorbet-runtime'
class C1
  extend T::Sig

  sig { returns(T.any(C1, C2)) }
  def m1
    return self if rand > 0.5
    C2.new
  end

  sig { returns(String) }
  def x
#     ^ apply-rename: [A] newName: y
    "hello from c1"
  end
end

class C2
  extend T::Sig

  # This needs to get renamed even though C2 is unrelated to C1 by subtyping
  sig { returns(String) }
  def x
    "hello from c2"
  end
end

c1 = C1.new
puts c1.m1.x
