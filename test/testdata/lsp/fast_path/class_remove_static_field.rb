# typed: true

class A extend T::Sig
  sig {params(x: Integer).returns(String)}
  def bar(x)
    x.to_s
  end

  FOO = T.let(0, Integer)
end

A.new.bar # error: Not enough arguments provided for method `A#bar`. Expected: `1`, got: `0`
p(A::FOO.nope)
#        ^^^^ error: Method `nope` does not exist on `Integer`
