# frozen_string_literal: true
# typed: true
# compiled: true

extend T::Sig

class SomethingElse
  def -(arg0)
    "got arg: #{arg0}"
  end
end

sig {params(x: T.any(Integer, SomethingElse), y: T.untyped).returns(T.untyped)}
def do_minus(x, y)
  x - y
end


def do_minus_untyped(x, y)
  x - y
end


p do_minus(4, 5)
p do_minus(8, 9.0)
p do_minus(SomethingElse.new, :foo)

p do_minus_untyped(4, 5)
p do_minus_untyped(7, 3.0)
p do_minus_untyped(SomethingElse.new, [:foo])
