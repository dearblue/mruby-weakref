#!ruby

assert "WeakRef" do
  src = "abcdefg"
  ref = WeakRef.new(src)

  assert_equal src.inspect, ref.inspect

  ref = WeakRef.new(src.dup)

  assert_equal src.inspect, ref.inspect

  20.times { GC.start }

  assert_raise(WeakRef::RefError) { ref.inspect }

  # more test!
end
